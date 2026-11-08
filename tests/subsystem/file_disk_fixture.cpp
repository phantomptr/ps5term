// PS5Term GPL-3.0-or-later. Actual Linux/file-backed disk integration fixture.
#include "platform/linux_data_disk.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
static char directory[] = "/tmp/ps5term-file-guest-XXXXXX";
static int parent = -1, seed = -1;
static size_t disk_size;
static term::platform::LinuxDataDisk disk;
extern "C" Ps5LinuxDisk test_open_disk(const void *bytes, size_t size)
{
    assert(mkdtemp(directory));
    parent = open(directory, O_RDONLY | O_DIRECTORY);
    assert(parent >= 0);
    seed = openat(parent, "seed", O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(seed >= 0);
    auto *p = static_cast<const unsigned char *>(bytes);
    for (size_t offset = 0; offset < size;)
    {
        auto n = write(seed, p + offset, size - offset);
        assert(n > 0);
        offset += static_cast<size_t>(n);
    }
    disk_size = size;
    assert(disk.open(parent, seed, size));
    return disk.device();
}
extern "C" Ps5LinuxDisk test_reopen_disk()
{
    disk.close();
    assert(disk.open(parent, seed, disk_size));
    return disk.device();
}
extern "C" void test_close_disk()
{
    disk.close();
    close(seed);
    assert(!unlinkat(parent, "seed", 0));
    assert(!unlinkat(parent, "data.ext2", 0));
    close(parent);
    assert(!rmdir(directory));
}
