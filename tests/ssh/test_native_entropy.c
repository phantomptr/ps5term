/* GPL-3.0-or-later. Fault-injected kernel-device adapter contract. */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
static int mode, reads, closed;
int test_open(const char *path, int flags, ...)
{
    (void)flags;
    assert(strcmp(path, "/dev/random") == 0);
    return mode == 1 ? -1 : 5;
}
int test_fstat(int fd, struct stat *info)
{
    assert(fd == 5);
    info->st_mode = mode == 6 ? S_IFREG : S_IFCHR;
    return mode == 7 ? -1 : 0;
}
ssize_t test_read(int fd, void *bytes, size_t size)
{
    assert(fd == 5);
    ++reads;
    if (mode == 2)
    {
        errno = EIO;
        return -1;
    }
    if (mode == 3 && reads == 1)
    {
        errno = EINTR;
        return -1;
    }
    if (mode == 4 && reads > 1)
        return 0;
    const size_t n = size > 7 ? 7 : size;
    memset(bytes, 0x7a, n);
    return n;
}
int test_close(int fd)
{
    assert(fd == 5);
    ++closed;
    return mode == 5 ? -1 : 0;
}
int ps5term_native_seed(unsigned char *, unsigned int);
int main(void)
{
    unsigned char data[24];
    assert(ps5term_native_seed(NULL, 1) == -1);
    assert(ps5term_native_seed(data, 0) == -1);
    assert(ps5term_native_seed(data, 1025) == -1);
    for (mode = 0; mode <= 7; ++mode)
    {
        memset(data, 0, sizeof data);
        reads = closed = 0;
        const int result = ps5term_native_seed(data, sizeof data);
        assert(result == ((mode == 0 || mode == 3) ? 0 : -1));
        if (mode != 1)
            assert(closed == 1);
        for (unsigned i = 0; i < sizeof data; ++i)
            assert(data[i] == ((mode == 0 || mode == 3) ? 0x7a : 0));
    }
}
