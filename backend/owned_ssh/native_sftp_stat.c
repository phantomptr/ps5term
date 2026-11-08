/* PS5Term - GPL-3.0-or-later. No-follow metadata fallback for native libc. */
#include "native_sftp_stat.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
int ps5term_sftp_lstat(const char *path, struct stat *result)
{
    struct stat preliminary;
    int descriptor, ret, saved;
    if (lstat(path, result) == 0) return 0;
    /* Some PS5 app contexts reject lstat even for ordinary readable files.
     * Do not substitute stat (which follows links) as LSTAT's result. Only
     * open regular files/directories, without following the final component,
     * and report the descriptor's metadata. Links and other types stay errors
     * when the platform's real lstat is unavailable. This is not confinement. */
    if (stat(path, &preliminary) != 0) return -1;
    if (!S_ISREG(preliminary.st_mode) && !S_ISDIR(preliminary.st_mode)) {
        errno = EOPNOTSUPP;
        return -1;
    }
    descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) return -1;
    ret = fstat(descriptor, result);
    saved = errno;
    if (ret == 0 && !S_ISREG(result->st_mode) && !S_ISDIR(result->st_mode)) {
        ret = -1; saved = EOPNOTSUPP;
    }
    if (close(descriptor) != 0 && ret == 0) return -1;
    errno = saved;
    return ret;
}

int ps5term_sftp_chown(const char *path, uid_t owner, gid_t group)
{
    (void)path; (void)owner; (void)group;
    errno = EPERM;
    return -1;
}

int ps5term_sftp_fchown(int descriptor, uid_t owner, gid_t group)
{
    (void)descriptor; (void)owner; (void)group;
    errno = EPERM;
    return -1;
}
