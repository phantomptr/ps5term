/* Forced missing-native-lstat regression. GPL-3.0-or-later. */
#include "backend/owned_ssh/native_sftp_stat.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int test_missing_lstat(const char *path, struct stat *result)
{
    (void)path;
    (void)result;
    errno = ENOSYS;
    return -1;
}
int main(void)
{
    char root[] = "/tmp/ps5term-sftp-stat-XXXXXX", file[256], link[256], pipe[256];
    struct stat info;
    assert(mkdtemp(root));
    snprintf(file, sizeof(file), "%s/file", root);
    snprintf(link, sizeof(link), "%s/link", root);
    snprintf(pipe, sizeof(pipe), "%s/pipe", root);
    int fd = open(file, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0);
    assert(write(fd, "abc", 3) == 3);
    assert(close(fd) == 0);
    assert(ps5term_sftp_lstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 3);
    assert(ps5term_sftp_lstat(root, &info) == 0 && S_ISDIR(info.st_mode));
    assert(symlink(file, link) == 0);
    assert(ps5term_sftp_lstat(link, &info) == -1); /* Never present target as link. */
    assert(mkfifo(pipe, 0600) == 0);
    assert(ps5term_sftp_lstat(pipe, &info) == -1 && errno == EOPNOTSUPP);
    assert(unlink(file) == 0);
    assert(ps5term_sftp_lstat(file, &info) == -1 && errno == ENOENT);
    assert(unlink(link) == 0 && unlink(pipe) == 0 && rmdir(root) == 0);
    /* The SDK's own chown would kill the app; these must refuse, not crash. */
    assert(ps5term_sftp_chown("/data", 0, 0) == -1 && errno == EPERM);
    assert(ps5term_sftp_fchown(0, 0, 0) == -1 && errno == EPERM);
}
