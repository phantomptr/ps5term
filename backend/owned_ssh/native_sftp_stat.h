/* PS5Term native metadata adapter. GPL-3.0-or-later. */
#pragma once
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
int ps5term_sftp_lstat(const char *path, struct stat *result);
/* The SDK implements chown and fchown as raw syscalls in libc.a, which the app
 * context refuses by killing the process. Ownership cannot be changed from here. */
int ps5term_sftp_chown(const char *path, uid_t owner, gid_t group);
int ps5term_sftp_fchown(int descriptor, uid_t owner, gid_t group);
#ifdef __cplusplus
}
#endif
