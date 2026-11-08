#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

#include "ps5sshd.h"
#include "shell.h"


#define LOGIN_GRACE_SECONDS 120
#define MAX_AUTH_ATTEMPTS   3
#define AUTH_FAIL_DELAY_US  (2 * 1000 * 1000)


/**
 * Wait until fd is readable (or writable when want_write is set).
 * Returns >0 when ready, 0 on timeout and -1 when the socket is dead.
 **/
static int
wait_socket(int fd, int want_write, int timeout_ms) {
  struct pollfd pfd;
  int rc;

  pfd.fd = fd;
  pfd.events = POLLIN | (want_write ? POLLOUT : 0);
  pfd.revents = 0;

  rc = poll(&pfd, 1, timeout_ms);
  if(rc < 0) {
    return errno == EINTR ? 0 : -1;
  }
  if(rc > 0 && (pfd.revents & (POLLERR | POLLNVAL))) {
    return -1;
  }

  return rc;
}


static int
const_time_eq(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen) {
  uint8_t diff = alen != blen;
  size_t n = alen < blen ? alen : blen;

  for(size_t i=0; i<n; i++) {
    diff |= a[i] ^ b[i];
  }

  return diff == 0;
}


static int
auth_cb(byte type, WS_UserAuthData *data, void *ctx) {
  ps5sshd_config_t *cfg = ctx;

  if(!cfg || !data) {
    return WOLFSSH_USERAUTH_FAILURE;
  }

  if(type == WOLFSSH_USERAUTH_PASSWORD) {
    const WS_UserAuthData_Password *pw = &data->sf.password;

    if(!cfg->have_password || !pw->password) {
      return WOLFSSH_USERAUTH_FAILURE;
    }
    if(const_time_eq(pw->password, pw->passwordSz,
                     (const uint8_t*)cfg->password, strlen(cfg->password))) {
      return WOLFSSH_USERAUTH_SUCCESS;
    }
    log_printf("password rejected for user '%.*s'", (int)data->usernameSz,
               (const char*)data->username);
    cfg->password_failures++;
    usleep(AUTH_FAIL_DELAY_US);
    return WOLFSSH_USERAUTH_INVALID_PASSWORD;
  }

  if(type == WOLFSSH_USERAUTH_PUBLICKEY) {
    const WS_UserAuthData_PublicKey *pk = &data->sf.publicKey;

    for(int i=0; i<cfg->nkeys; i++) {
      if(const_time_eq(pk->publicKey, pk->publicKeySz,
                       cfg->keys[i].blob, cfg->keys[i].len)) {
        return WOLFSSH_USERAUTH_SUCCESS;
      }
    }
    return WOLFSSH_USERAUTH_INVALID_PUBLICKEY;
  }

  return WOLFSSH_USERAUTH_FAILURE;
}


static int
is_retry(int error) {
  return error == WS_WANT_READ || error == WS_WANT_WRITE;
}


static int
do_accept(WOLFSSH *ssh, int fd) {
  time_t deadline = time(NULL) + LOGIN_GRACE_SECONDS;
  int ret, error;

  ret = wolfSSH_accept(ssh);
  error = wolfSSH_get_error(ssh);
  while(ret != WS_SUCCESS && ret != WS_SFTP_COMPLETE && ret != WS_SCP_INIT &&
        is_retry(error)) {
    int rc;

    if(time(NULL) > deadline) {
      log_printf("login grace time expired");
      return WS_FATAL_ERROR;
    }
    rc = wait_socket(fd, error == WS_WANT_WRITE ||
                     wolfSSH_OutputPending(ssh), 1000);
    if(rc < 0) {
      return WS_FATAL_ERROR;
    }
    if(rc == 0 && error == WS_WANT_READ) {
      continue;
    }
    ret = wolfSSH_accept(ssh);
    error = wolfSSH_get_error(ssh);
  }

  return ret;
}


static int
run_scp(WOLFSSH *ssh, int fd) {
  int ret, error;

  ret = wolfSSH_accept(ssh);
  error = wolfSSH_get_error(ssh);
  while(ret != WS_SUCCESS && ret != WS_SCP_COMPLETE && is_retry(error)) {
    if(wait_socket(fd, error == WS_WANT_WRITE, 1000) < 0) {
      return WS_FATAL_ERROR;
    }
    ret = wolfSSH_accept(ssh);
    error = wolfSSH_get_error(ssh);
  }

  return ret == WS_SCP_COMPLETE ? WS_SUCCESS : ret;
}


/* Mirrors the SFTP service loop of wolfsshd. */
static int
run_sftp(WOLFSSH *ssh, int fd) {
  int ret = WS_SUCCESS;
  int timeout_ms = 0;
  int error;
  byte peek;

  do {
    int rc;

    if(wolfSSH_SFTP_PendingSend(ssh)) {
      ret = wolfSSH_SFTP_read(ssh);
      error = wolfSSH_get_error(ssh);
      timeout_ms = (ret == WS_REKEYING) ? 1000 : 0;
      if(error == WS_WANT_READ || error == WS_WANT_WRITE ||
         error == WS_CHAN_RXD || error == WS_REKEYING ||
         error == WS_WINDOW_FULL) {
        ret = error;
      }
      if(error == WS_WANT_WRITE && wolfSSH_SFTP_PendingSend(ssh)) {
        if(wait_socket(fd, 1, 1000) < 0) {
          break;
        }
        continue;
      }
      if(error == WS_EOF) {
        ret = 0;
        break;
      }
    }

    rc = wait_socket(fd, wolfSSH_OutputPending(ssh), timeout_ms);
    if(rc < 0) {
      break;
    }

    if(ret == WS_WANT_READ || ret == WS_WANT_WRITE || rc > 0) {
      ret = wolfSSH_worker(ssh, NULL);
      error = wolfSSH_get_error(ssh);
      if(ret == WS_REKEYING) {
        timeout_ms = 1000;
        continue;
      }

      if(ret != WS_CHAN_RXD && (error == WS_WANT_READ ||
         error == WS_WANT_WRITE || error == WS_WINDOW_FULL)) {
        timeout_ms = 1000;
        ret = error;
        continue;
      }

      if(error == WS_EOF) {
        int peekRet = wolfSSH_stream_peek(ssh, NULL, 1);
        if(peekRet != WS_REKEYING && peekRet <= 0) {
          ret = 0;
          break;
        }
      }
      if(ret != WS_SUCCESS && ret != WS_CHAN_RXD && ret != WS_EOF) {
        break;
      }
    }

    ret = wolfSSH_stream_peek(ssh, &peek, sizeof(peek));
    if(ret > 0) {
      ret = wolfSSH_SFTP_read(ssh);
      error = wolfSSH_get_error(ssh);
      timeout_ms = (ret == WS_REKEYING) ? 1000 : 0;
      if(error == WS_WANT_READ || error == WS_WANT_WRITE ||
         error == WS_CHAN_RXD || error == WS_REKEYING ||
         error == WS_WINDOW_FULL) {
        ret = error;
      }
      if(error == WS_EOF) {
        ret = 0;
        break;
      }
      continue;
    } else if(ret == WS_REKEYING) {
      timeout_ms = 1000;
      continue;
    } else if(ret < 0) {
      error = wolfSSH_get_error(ssh);
      if(error == WS_EOF) {
        ret = 0;
        break;
      }
    } else {
      timeout_ms = 1000;
      continue;
    }

    if(ret == WS_FATAL_ERROR && error == 0) {
      WOLFSSH_CHANNEL* channel = wolfSSH_ChannelNext(ssh, NULL);
      if(channel && wolfSSH_ChannelGetEof(channel)) {
        ret = 0;
        break;
      }
    }
  } while(ret != WS_FATAL_ERROR);

  return ret;
}


static void
graceful_shutdown(WOLFSSH *ssh, int fd) {
  int error = wolfSSH_get_error(ssh);
  int ret;

  if(error == WS_SOCKET_ERROR_E || error == WS_FATAL_ERROR) {
    return;
  }

  ret = wolfSSH_shutdown(ssh);
  if(ret == WS_SOCKET_ERROR_E) {
    return;
  }

  error = wolfSSH_get_error(ssh);
  if(error == WS_WANT_READ || error == WS_WANT_WRITE) {
    for(int attempt=0; attempt<20; attempt++) {
      wait_socket(fd, wolfSSH_OutputPending(ssh), 100);
      ret = wolfSSH_worker(ssh, NULL);
      error = wolfSSH_get_error(ssh);
      if(ret == WS_CHANNEL_CLOSED || ret == WS_EOF ||
         ret == WS_SOCKET_ERROR_E) {
        break;
      }
      if(ret == WS_FATAL_ERROR && !is_retry(error)) {
        break;
      }
    }
  }
}


static int
load_hostkeys(WOLFSSH_CTX *ctx, ps5sshd_config_t *cfg) {
  int loaded = 0;

  if(cfg->hostkey_ed25519 &&
     wolfSSH_CTX_UsePrivateKey_buffer(ctx, cfg->hostkey_ed25519,
                                      cfg->hostkey_ed25519_len,
                                      WOLFSSH_FORMAT_ASN1) == WS_SUCCESS) {
    loaded++;
  }
  if(cfg->hostkey_ecdsa &&
     wolfSSH_CTX_UsePrivateKey_buffer(ctx, cfg->hostkey_ecdsa,
                                      cfg->hostkey_ecdsa_len,
                                      WOLFSSH_FORMAT_ASN1) == WS_SUCCESS) {
    loaded++;
  }

  return loaded;
}


int
session_run(int fd, const char *peer) {
  ps5sshd_config_t cfg;
  WOLFSSH_CTX *ctx = NULL;
  WOLFSSH *ssh = NULL;
  int result = SESSION_OK;
  int ret;

#ifndef PS5SSHD_HOST
  syscall(SYS_thr_set_name, -1, "ps5sshd-session");
#endif
  signal(SIGCHLD, SIG_DFL);
  signal(SIGPIPE, SIG_IGN);

  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &(int){1}, sizeof(int));
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int));

  config_load(&cfg);
  if(!cfg.have_password && !cfg.nkeys) {
    log_printf("no credentials configured, refusing %s", peer);
    goto out;
  }

  if(wolfSSH_Init() != WS_SUCCESS) {
    log_printf("wolfSSH_Init failed");
    goto out;
  }

  if(!(ctx=wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL))) {
    log_printf("wolfSSH_CTX_new failed");
    goto out;
  }
  wolfSSH_SetUserAuth(ctx, auth_cb);
  wolfSSH_CTX_SetMaxAuthAttempts(ctx, MAX_AUTH_ATTEMPTS);
  if(!load_hostkeys(ctx, &cfg)) {
    log_printf("no usable host key");
    goto out;
  }

  if(!(ssh=wolfSSH_new(ctx))) {
    log_printf("wolfSSH_new failed");
    goto out;
  }
  wolfSSH_SetUserAuthCtx(ssh, &cfg);

  /* start SFTP sessions in PS5SSHD_HOME; the chdir also covers wolfSSH's
     getcwd() fallback */
  if(chdir(PS5SSHD_HOME) != 0 ||
     wolfSSH_SFTP_SetDefaultPath(ssh, PS5SSHD_HOME) != WS_SUCCESS) {
    log_printf("unable to use %s as starting directory", PS5SSHD_HOME);
  }
  wolfSSH_set_fd(ssh, fd);
  set_nonblocking(fd);

  ret = do_accept(ssh, fd);
  if(ret != WS_SUCCESS && ret != WS_SFTP_COMPLETE && ret != WS_SCP_INIT) {
    log_printf("handshake with %s failed: %s", peer,
               wolfSSH_ErrorToName(wolfSSH_get_error(ssh)));
    if(cfg.password_failures) {
      result = SESSION_AUTH_FAILED;
    }
    goto out;
  }

  log_printf("%s authenticated as '%s'", peer,
             wolfSSH_GetUsername(ssh) ? wolfSSH_GetUsername(ssh) : "");

  if(ret == WS_SFTP_COMPLETE) {
    log_printf("sftp session for %s", peer);
    run_sftp(ssh, fd);
  } else if(ret == WS_SCP_INIT) {
    log_printf("scp session for %s", peer);
    run_scp(ssh, fd);
  } else {
    switch(wolfSSH_GetSessionType(ssh)) {
    case WOLFSSH_SESSION_SHELL:
      log_printf("shell session for %s", peer);
      shell_run(ssh, fd, NULL);
      break;

    case WOLFSSH_SESSION_EXEC:
      log_printf("exec session for %s: %s", peer,
                 wolfSSH_GetSessionCommand(ssh));
      shell_run(ssh, fd, wolfSSH_GetSessionCommand(ssh));
      break;

    default:
      log_printf("unsupported session type %d (%s)",
                 wolfSSH_GetSessionType(ssh),
                 wolfSSH_GetSessionCommand(ssh) ?
                 wolfSSH_GetSessionCommand(ssh) : "");
      break;
    }
  }

  graceful_shutdown(ssh, fd);
  log_printf("%s disconnected", peer);

 out:
  if(ssh) {
    wolfSSH_free(ssh);
  }
  sftpio_flush_all();
  if(ctx) {
    wolfSSH_CTX_free(ctx);
  }
  wolfSSH_Cleanup();
  config_free(&cfg);
  shutdown(fd, SHUT_RDWR);
  close(fd);

  return result;
}
