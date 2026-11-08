/* PS5Term native wolfSSH directory adapter. GPL-3.0-or-later. */
#ifndef PS5TERM_NATIVE_SFTP_DIRECTORY_H
#define PS5TERM_NATIVE_SFTP_DIRECTORY_H
#include <dirent.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ps5term_sftp_directory;
struct ps5term_sftp_directory *ps5term_sftp_opendir(const char *path);
/* Takes ownership of descriptor only on success, like fdopendir. */
struct ps5term_sftp_directory *ps5term_sftp_fdopendir(int descriptor);
struct dirent *ps5term_sftp_readdir(struct ps5term_sftp_directory *directory);
int ps5term_sftp_closedir(struct ps5term_sftp_directory *directory);
void ps5term_sftp_rewinddir(struct ps5term_sftp_directory *directory);
#ifdef __cplusplus
}
#endif
#endif
