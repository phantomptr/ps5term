#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PS5SSHD_VERSION   "1.1.0"
#ifndef PS5SSHD_PORT
#define PS5SSHD_PORT      22
#endif
#ifndef PS5SSHD_PORT_FALLBACK
#define PS5SSHD_PORT_FALLBACK 2222
#endif
#ifndef PS5SSHD_DIR
#define PS5SSHD_DIR       "/data/ps5sshd"
#endif
#define PS5SSHD_LOG       PS5SSHD_DIR "/ps5sshd.log"
#define PS5SSHD_AUTHKEYS  PS5SSHD_DIR "/authorized_keys"
#define PS5SSHD_PASSWORD  PS5SSHD_DIR "/password"
#define PS5SSHD_HOSTKEY_ED25519 PS5SSHD_DIR "/ssh_host_ed25519_key.der"
#define PS5SSHD_HOSTKEY_ECDSA   PS5SSHD_DIR "/ssh_host_ecdsa_key.der"
/* starting directory for shells and SFTP */
#ifndef PS5SSHD_HOME
#define PS5SSHD_HOME      "/data"
#endif

#define PS5SSHD_MAX_KEYS  64

typedef struct ps5sshd_key {
  uint8_t *blob;
  size_t   len;
} ps5sshd_key_t;

typedef struct ps5sshd_config {
  char         password[256];
  int          have_password;
  ps5sshd_key_t keys[PS5SSHD_MAX_KEYS];
  int          nkeys;
  int          password_failures;
  uint8_t     *hostkey_ed25519;
  size_t       hostkey_ed25519_len;
  uint8_t     *hostkey_ecdsa;
  size_t       hostkey_ecdsa_len;
} ps5sshd_config_t;

/* util.c */
void  log_open(void);
void  log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void  notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int   read_file(const char *path, uint8_t **buf, size_t *len);
int   write_file(const char *path, const void *buf, size_t len, int mode);
int   set_nonblocking(int fd);

/* config.c */
int   config_init_dir(void);
int   config_ensure_hostkeys(void);
int   config_ensure_credentials(void);
int   config_load(ps5sshd_config_t *cfg);
void  config_free(ps5sshd_config_t *cfg);

/* sftp_io.c */
void  sftpio_flush_all(void);

/* session.c: returns SESSION_AUTH_FAILED when the peer gave a wrong
   password and never authenticated */
#define SESSION_OK          0
#define SESSION_AUTH_FAILED 2
int   session_run(int fd, const char *peer);

/* shell.c (needs wolfSSH types, see shell.h) */
