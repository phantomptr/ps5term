#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#include "elfldr.h"
#include "ps5sshd.h"
#include "shell.h"

#include "ps5sh.elf.inc"


#define IO_CHUNK      16384
#define BACKLOG_LIMIT (256 * 1024)
#define LINE_MAX_LEN  4096


typedef struct buf {
  uint8_t *data;
  size_t   len;
  size_t   cap;
} buf_t;


typedef struct shell {
  WOLFSSH *ssh;
  word32   chid;
  int      pty;
  pid_t    pid;

  int      in_fd;
  int      out_fd;
  int      err_fd;

  buf_t    to_child;
  buf_t    to_client;
  buf_t    to_client_err;

  int      close_stdin;
  int      peer_eof;
  int      peer_gone;

  uint8_t  line[LINE_MAX_LEN];
  size_t   linelen;
  int      esc;
  int      in_last_cr;
  int      out_last_cr;
} shell_t;


static int
buf_append(buf_t *b, const void *data, size_t len) {
  if(b->len + len > b->cap) {
    size_t cap = b->cap ? b->cap : 4096;
    uint8_t *p;

    while(cap < b->len + len) {
      cap *= 2;
    }
    if(!(p=realloc(b->data, cap))) {
      return -1;
    }
    b->data = p;
    b->cap = cap;
  }
  memcpy(b->data + b->len, data, len);
  b->len += len;

  return 0;
}


static void
buf_consume(buf_t *b, size_t len) {
  if(len >= b->len) {
    b->len = 0;
    return;
  }
  memmove(b->data, b->data + len, b->len - len);
  b->len -= len;
}


static void
buf_free(buf_t *b) {
  free(b->data);
  memset(b, 0, sizeof(*b));
}


static void
echo(shell_t *sh, const char *s, size_t len) {
  buf_append(&sh->to_client, s, len);
}


static void
interrupt_child(shell_t *sh) {
  if(sh->pid > 0) {
    kill(sh->pid, SIGUSR1);
  }
}


/**
 * Minimal canonical-mode line discipline, used when the client asked for a
 * pty. The PS5 has no pseudo terminals, so echo and editing happen here.
 **/
static void
ld_input(shell_t *sh, const uint8_t *data, size_t len) {
  for(size_t i=0; i<len; i++) {
    uint8_t c = data[i];

    if(sh->esc == 1) {
      sh->esc = (c == '[' || c == 'O') ? 2 : 0;
      continue;
    }
    if(sh->esc == 2) {
      if(c >= 0x40 && c <= 0x7e) {
        sh->esc = 0;
      }
      continue;
    }

    if(c == '\n' && sh->in_last_cr) {
      sh->in_last_cr = 0;
      continue;
    }
    sh->in_last_cr = (c == '\r');

    switch(c) {
    case '\r':
    case '\n':
      echo(sh, "\r\n", 2);
      sh->line[sh->linelen++] = '\n';
      buf_append(&sh->to_child, sh->line, sh->linelen);
      sh->linelen = 0;
      break;

    case 0x7f:
    case 0x08:
      if(sh->linelen) {
        do {
          sh->linelen--;
        } while(sh->linelen && (sh->line[sh->linelen] & 0xc0) == 0x80);
        echo(sh, "\b \b", 3);
      }
      break;

    case 0x03:
      echo(sh, "^C\r\n", 4);
      sh->linelen = 0;
      interrupt_child(sh);
      break;

    case 0x04:
      if(!sh->linelen) {
        sh->close_stdin = 1;
      }
      break;

    case 0x15:
      while(sh->linelen) {
        do {
          sh->linelen--;
        } while(sh->linelen && (sh->line[sh->linelen] & 0xc0) == 0x80);
        echo(sh, "\b \b", 3);
      }
      break;

    case 0x17:
      while(sh->linelen && sh->line[sh->linelen - 1] == ' ') {
        sh->linelen--;
        echo(sh, "\b \b", 3);
      }
      while(sh->linelen && sh->line[sh->linelen - 1] != ' ') {
        do {
          sh->linelen--;
        } while(sh->linelen && (sh->line[sh->linelen] & 0xc0) == 0x80);
        echo(sh, "\b \b", 3);
      }
      break;

    case 0x1b:
      sh->esc = 1;
      break;

    default:
      if(c < 0x20 && c != '\t') {
        break;
      }
      if(sh->linelen < LINE_MAX_LEN - 1) {
        sh->line[sh->linelen++] = c;
        echo(sh, (const char*)&c, 1);
      }
      break;
    }
  }
}


static void
client_input(shell_t *sh, const uint8_t *data, size_t len) {
  if(sh->pty) {
    ld_input(sh, data, len);
  } else if(sh->in_fd >= 0) {
    buf_append(&sh->to_child, data, len);
  }
}


static void
child_output(shell_t *sh, buf_t *dst, const uint8_t *data, size_t len) {
  if(!sh->pty) {
    buf_append(dst, data, len);
    return;
  }

  for(size_t i=0; i<len; i++) {
    if(data[i] == '\n' && !sh->out_last_cr) {
      buf_append(dst, "\r", 1);
    }
    sh->out_last_cr = (data[i] == '\r');
    buf_append(dst, &data[i], 1);
  }
}


/* Returns -1 when the channel can never take the data. */
static int
flush_buf(shell_t *sh, buf_t *b, int ext) {
  while(b->len) {
    word32 n = b->len > IO_CHUNK ? IO_CHUNK : (word32)b->len;
    int rc;

    if(ext) {
      rc = wolfSSH_ChannelIdSendExt(sh->ssh, sh->chid, b->data, n);
    } else {
      rc = wolfSSH_ChannelIdSend(sh->ssh, sh->chid, b->data, n);
    }

    if(rc > 0) {
      buf_consume(b, rc);
      continue;
    }
    if(rc == WS_WINDOW_FULL || rc == WS_WANT_WRITE || rc == WS_REKEYING ||
       rc == 0) {
      return 0;
    }
    return -1;
  }

  return 0;
}


static void
flush_child(shell_t *sh) {
  while(sh->to_child.len && sh->in_fd >= 0) {
    ssize_t n = write(sh->in_fd, sh->to_child.data, sh->to_child.len);
    if(n > 0) {
      buf_consume(&sh->to_child, n);
      continue;
    }
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    close(sh->in_fd);
    sh->in_fd = -1;
    sh->to_child.len = 0;
  }

  if(sh->close_stdin && !sh->to_child.len && sh->in_fd >= 0) {
    close(sh->in_fd);
    sh->in_fd = -1;
  }
}


/* Returns 1 on EOF. */
static int
read_child(shell_t *sh, int *fd, buf_t *dst) {
  uint8_t buf[IO_CHUNK];
  ssize_t n = read(*fd, buf, sizeof(buf));

  if(n > 0) {
    child_output(sh, dst, buf, n);
    return 0;
  }
  if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
    return 0;
  }
  close(*fd);
  *fd = -1;

  return 1;
}


static void
service_channel(shell_t *sh) {
  int rc = wolfSSH_worker(sh->ssh, NULL);
  int error = wolfSSH_get_error(sh->ssh);

  if(rc == WS_CHANNEL_CLOSED) {
    sh->peer_gone = 1;
    return;
  }
  if(rc >= 0 || rc == WS_CHAN_RXD || rc == WS_EOF || rc == WS_REKEYING ||
     rc == WS_WANT_WRITE || rc == WS_WINDOW_FULL) {
    return;
  }
  if(rc == WS_FATAL_ERROR && (error == WS_WANT_READ ||
                              error == WS_WANT_WRITE)) {
    return;
  }
  sh->peer_gone = 1;
}


static void
read_channel(shell_t *sh) {
  uint8_t buf[IO_CHUNK];
  int n;

  if(sh->peer_eof || sh->to_child.len > BACKLOG_LIMIT) {
    return;
  }

  n = wolfSSH_ChannelIdPeek(sh->ssh, sh->chid, NULL, sizeof(buf));
  if(n > 0) {
    n = wolfSSH_ChannelIdRead(sh->ssh, sh->chid, buf, sizeof(buf));
    if(n > 0) {
      client_input(sh, buf, n);
    }
    return;
  }
  if(n == WS_INVALID_CHANID) {
    sh->peer_gone = 1;
    return;
  }
  if(n < 0 && wolfSSH_get_error(sh->ssh) == WS_EOF) {
    sh->peer_eof = 1;
    if(!sh->pty) {
      sh->close_stdin = 1;
    }
  } else if(n < 0 && wolfSSH_get_error(sh->ssh) == WS_DISCONNECT) {
    sh->peer_gone = 1;
  }
}


static pid_t
spawn_shell(shell_t *sh, const char *cmd) {
  int in[2] = {-1, -1}, out[2] = {-1, -1}, err[2] = {-1, -1};
  char *argv[4] = {"sh", NULL, NULL, NULL};
  pid_t pid;

  if(pipe(in) || pipe(out) || (!sh->pty && pipe(err))) {
    log_printf("pipe: %s", strerror(errno));
    goto fail;
  }

  if(cmd) {
    argv[1] = "-c";
    argv[2] = (char*)cmd;
  } else if(!sh->pty) {
    argv[1] = "-q";
  }

  pid = elfldr_spawn(in[0], out[1], sh->pty ? out[1] : err[1], ps5sh_elf,
                     argv);
  if(pid < 0) {
    log_printf("unable to spawn shell");
    goto fail;
  }

  close(in[0]);
  close(out[1]);
  if(err[1] >= 0) {
    close(err[1]);
  }

  sh->in_fd = in[1];
  sh->out_fd = out[0];
  sh->err_fd = err[0];
  set_nonblocking(sh->in_fd);
  set_nonblocking(sh->out_fd);
  if(sh->err_fd >= 0) {
    set_nonblocking(sh->err_fd);
  }

  return pid;

 fail:
  for(int i=0; i<2; i++) {
    if(in[i] >= 0) close(in[i]);
    if(out[i] >= 0) close(out[i]);
    if(err[i] >= 0) close(err[i]);
  }
  return -1;
}


int
shell_run(WOLFSSH *ssh, int fd, const char *cmd) {
  WOLFSSH_CHANNEL *channel;
  int child_done = 0;
  int status = 255;
  shell_t sh;

  memset(&sh, 0, sizeof(sh));
  sh.ssh = ssh;
  sh.in_fd = sh.out_fd = sh.err_fd = -1;

  if(!(channel=wolfSSH_ChannelNext(ssh, NULL)) ||
     wolfSSH_ChannelGetId(channel, &sh.chid, WS_CHANNEL_ID_SELF)
     != WS_SUCCESS) {
    log_printf("no session channel");
    return status;
  }
  sh.pty = wolfSSH_ChannelIsPty(channel) > 0;

  if(chdir(PS5SSHD_HOME) == 0) {
    setenv("PWD", PS5SSHD_HOME, 1);
  } else {
    chdir("/");
    setenv("PWD", "/", 1);
  }
  setenv("HOME", PS5SSHD_HOME, 1);
  setenv("PATH", PS5SSHD_DIR "/bin:/mnt/usb0/homebrew/bin:/user/homebrew/bin",
         1);

  if((sh.pid=spawn_shell(&sh, cmd)) < 0) {
    const char msg[] = "ps5sshd: unable to start shell\r\n";
    buf_append(&sh.to_client_err, msg, sizeof(msg) - 1);
    flush_buf(&sh, &sh.to_client_err, 1);
    wolfSSH_SetExitStatus(ssh, 127);
    return 127;
  }

  while(!sh.peer_gone) {
    struct pollfd pfds[4];
    int npfd = 0;
    int timeout = 250;
    int sock_idx, out_idx = -1, err_idx = -1, in_idx = -1;
    int wstatus;

    if(flush_buf(&sh, &sh.to_client, 0) || flush_buf(&sh, &sh.to_client_err,
                                                      1)) {
      break;
    }
    flush_child(&sh);

    if(!child_done && waitpid(sh.pid, &wstatus, WNOHANG) == sh.pid) {
      child_done = 1;
      if(WIFEXITED(wstatus)) {
        status = WEXITSTATUS(wstatus);
      } else if(WIFSIGNALED(wstatus)) {
        status = 128 + WTERMSIG(wstatus);
      }
    }

    if(sh.out_fd < 0 && sh.err_fd < 0 && !sh.to_client.len &&
       !sh.to_client_err.len) {
      if(child_done) {
        break;
      }
      timeout = 50;
    }

    sock_idx = npfd;
    pfds[npfd].fd = fd;
    pfds[npfd].events = POLLIN | (wolfSSH_OutputPending(ssh) ? POLLOUT : 0);
    pfds[npfd++].revents = 0;

    if(sh.out_fd >= 0 && sh.to_client.len < BACKLOG_LIMIT) {
      out_idx = npfd;
      pfds[npfd].fd = sh.out_fd;
      pfds[npfd].events = POLLIN;
      pfds[npfd++].revents = 0;
    }
    if(sh.err_fd >= 0 && sh.to_client_err.len < BACKLOG_LIMIT) {
      err_idx = npfd;
      pfds[npfd].fd = sh.err_fd;
      pfds[npfd].events = POLLIN;
      pfds[npfd++].revents = 0;
    }
    if(sh.in_fd >= 0 && sh.to_child.len) {
      in_idx = npfd;
      pfds[npfd].fd = sh.in_fd;
      pfds[npfd].events = POLLOUT;
      pfds[npfd++].revents = 0;
    }

    if(!sh.peer_eof && sh.to_child.len <= BACKLOG_LIMIT &&
       wolfSSH_ChannelIdPeek(ssh, sh.chid, NULL, 1) > 0) {
      timeout = 0;
    }

    if(poll(pfds, npfd, timeout) < 0 && errno != EINTR) {
      break;
    }

    if(pfds[sock_idx].revents & (POLLERR | POLLNVAL)) {
      sh.peer_gone = 1;
      break;
    }
    if(pfds[sock_idx].revents || wolfSSH_OutputPending(ssh) ||
       sh.to_client.len || sh.to_client_err.len) {
      service_channel(&sh);
    }
    read_channel(&sh);

    if(out_idx >= 0 && pfds[out_idx].revents) {
      read_child(&sh, &sh.out_fd, &sh.to_client);
    }
    if(err_idx >= 0 && pfds[err_idx].revents) {
      read_child(&sh, &sh.err_fd, &sh.to_client_err);
    }
    if(in_idx >= 0 && pfds[in_idx].revents) {
      flush_child(&sh);
    }
  }

  if(!child_done) {
    interrupt_child(&sh);
    kill(sh.pid, SIGKILL);
    waitpid(sh.pid, NULL, 0);
  }

  if(sh.in_fd >= 0) close(sh.in_fd);
  if(sh.out_fd >= 0) close(sh.out_fd);
  if(sh.err_fd >= 0) close(sh.err_fd);
  buf_free(&sh.to_child);
  buf_free(&sh.to_client);
  buf_free(&sh.to_client_err);

  wolfSSH_SetExitStatus(ssh, status);

  return status;
}
