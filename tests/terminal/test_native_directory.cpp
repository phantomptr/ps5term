// PS5Term - GPL-3.0-or-later.
#include "platform/directory.hpp"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
static int opened = -1;
static unsigned reads = 0;
extern "C" int getdirentries(int fd, char *buffer, int capacity, long *base)
{
    // Model the console app0 filesystem's minimum 64 KiB directory block.
    opened = fd;
    ++reads;
    struct stat info
    {
    };
    assert(fstat(fd, &info) == 0 && S_ISDIR(info.st_mode));
    if (capacity < 65536)
    {
        errno = EINVAL;
        return -1;
    }
    if (*base)
        return 0;
    std::memset(buffer, 0, 12);
    buffer[0] = 1;
    buffer[4] = 12;
    buffer[7] = 3;
    std::memcpy(buffer + 8, "abc", 4);
    *base = 12;
    return 12;
}
int main()
{
    term::platform::DirectoryCursor cursor;
    assert(cursor.open("/tmp"));
    std::string_view name;
    assert(cursor.next(name) == term::platform::DirectoryStep::entry && name == "abc");
    assert(reads == 1);
    assert(cursor.next(name) == term::platform::DirectoryStep::end);
    cursor.close();
    assert(fcntl(opened, F_GETFD) == -1 && errno == EBADF);
    assert(cursor.next(name) == term::platform::DirectoryStep::failed && errno == EBADF);
    assert(cursor.open("/tmp"));
    assert(cursor.next(name) == term::platform::DirectoryStep::entry && name == "abc");
    std::puts("native directory block and lifecycle tests passed");
}
