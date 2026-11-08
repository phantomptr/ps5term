#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssh/ssh.h>
#include <wolfssh/keygen.h>

#include "ps5sshd.h"


int
config_init_dir(void) {
  struct stat st;

  if(stat(PS5SSHD_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
    return 0;
  }
  if(mkdir(PS5SSHD_DIR, 0700) == 0) {
    return 0;
  }
  return stat(PS5SSHD_DIR, &st) == 0 ? 0 : -1;
}


static int
ensure_hostkey(const char *path, const char *name, int ed25519) {
  uint8_t der[512];
  struct stat st;
  int len;

  if(stat(path, &st) == 0 && st.st_size > 0) {
    return 0;
  }

  log_printf("generating %s host key", name);
  if(ed25519) {
    len = wolfSSH_MakeEd25519Key(der, sizeof(der), 256);
  } else {
    len = wolfSSH_MakeEcdsaKey(der, sizeof(der), 256);
  }
  if(len <= 0) {
    log_printf("unable to generate %s host key (%d)", name, len);
    return -1;
  }
  if(write_file(path, der, len, 0600)) {
    log_printf("unable to write %s: %s", path, strerror(errno));
    return -1;
  }

  return 0;
}


int
config_ensure_hostkeys(void) {
  int ok = 0;

  if(ensure_hostkey(PS5SSHD_HOSTKEY_ED25519, "ed25519", 1) == 0) {
    ok++;
  }
  if(ensure_hostkey(PS5SSHD_HOSTKEY_ECDSA, "ecdsa", 0) == 0) {
    ok++;
  }

  return ok ? 0 : -1;
}


static int
is_pin(const char *s) {
  for(int i=0; i<4; i++) {
    if(s[i] < '0' || s[i] > '9') {
      return 0;
    }
  }
  return s[4] == '\0';
}


/**
 * Make sure the user has some way to log in. If neither authorized_keys nor
 * a password file exists, generate a random 4 digit PIN. A PIN (generated or
 * not) is shown as a notification on every start so it can't get lost; a
 * custom password is never shown.
 **/
int
config_ensure_credentials(void) {
  ps5sshd_config_t cfg;
  char password[8];
  struct stat st;

  if(stat(PS5SSHD_PASSWORD, &st) != 0 && stat(PS5SSHD_AUTHKEYS, &st) != 0) {
    snprintf(password, sizeof(password), "%04u", arc4random_uniform(10000));
    if(write_file(PS5SSHD_PASSWORD, password, strlen(password), 0600)) {
      log_printf("unable to write %s: %s", PS5SSHD_PASSWORD, strerror(errno));
      return -1;
    }
    log_printf("generated password stored in " PS5SSHD_PASSWORD);
  }

  config_load(&cfg);
  if(cfg.have_password && is_pin(cfg.password)) {
    notify("ps5sshd password: %s", cfg.password);
  }
  config_free(&cfg);

  return 0;
}


static int
is_key_type(const char *s) {
  return !strncmp(s, "ssh-", 4) || !strncmp(s, "ecdsa-", 6) ||
         !strncmp(s, "rsa-", 4) || !strncmp(s, "sk-", 3);
}


static void
parse_authorized_keys(ps5sshd_config_t *cfg, char *text) {
  char *saveline = NULL;
  char *line;

  for(line=strtok_r(text, "\n", &saveline); line;
      line=strtok_r(NULL, "\n", &saveline)) {
    char *savetok = NULL;
    char *tok;
    char *prev = NULL;

    while(*line == ' ' || *line == '\t') {
      line++;
    }
    if(*line == '#' || *line == '\0' || *line == '\r') {
      continue;
    }

    for(tok=strtok_r(line, " \t\r", &savetok); tok;
        tok=strtok_r(NULL, " \t\r", &savetok)) {
      if(prev && is_key_type(prev)) {
        word32 outlen = (word32)strlen(tok);
        uint8_t *blob = malloc(outlen);

        if(!blob) {
          return;
        }
        if(Base64_Decode((const byte*)tok, (word32)strlen(tok), blob,
                         &outlen) != 0 || outlen == 0) {
          free(blob);
          break;
        }
        if(cfg->nkeys < PS5SSHD_MAX_KEYS) {
          cfg->keys[cfg->nkeys].blob = blob;
          cfg->keys[cfg->nkeys].len = outlen;
          cfg->nkeys++;
        } else {
          free(blob);
        }
        break;
      }
      prev = tok;
    }
  }
}


int
config_load(ps5sshd_config_t *cfg) {
  uint8_t *buf;
  size_t len;

  memset(cfg, 0, sizeof(*cfg));

  if(read_file(PS5SSHD_PASSWORD, &buf, &len) == 0) {
    char *nl = strpbrk((char*)buf, "\r\n");
    size_t n;

    if(nl) {
      *nl = 0;
    }
    n = strlen((char*)buf);
    while(n && (buf[n-1] == ' ' || buf[n-1] == '\t')) {
      buf[--n] = 0;
    }
    if(buf[0]) {
      strncpy(cfg->password, (char*)buf, sizeof(cfg->password) - 1);
      cfg->have_password = 1;
    }
    memset(buf, 0, len);
    free(buf);
  }

  if(read_file(PS5SSHD_AUTHKEYS, &buf, &len) == 0) {
    parse_authorized_keys(cfg, (char*)buf);
    free(buf);
  }

  if(read_file(PS5SSHD_HOSTKEY_ED25519, &buf, &len) == 0) {
    cfg->hostkey_ed25519 = buf;
    cfg->hostkey_ed25519_len = len;
  }
  if(read_file(PS5SSHD_HOSTKEY_ECDSA, &buf, &len) == 0) {
    cfg->hostkey_ecdsa = buf;
    cfg->hostkey_ecdsa_len = len;
  }

  return 0;
}


void
config_free(ps5sshd_config_t *cfg) {
  for(int i=0; i<cfg->nkeys; i++) {
    free(cfg->keys[i].blob);
  }
  free(cfg->hostkey_ed25519);
  free(cfg->hostkey_ecdsa);
  memset(cfg, 0, sizeof(*cfg));
}
