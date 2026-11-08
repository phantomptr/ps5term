// PS5Term GPL-3.0-or-later. Durable disk file ownership; no PS5 access.
#include "platform/linux_data_disk.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
int main()
{
    char directory[] = "/tmp/ps5term-data-XXXXXX";
    assert(mkdtemp(directory));
    int parent = open(directory, O_RDONLY | O_DIRECTORY);
    assert(parent >= 0);
    std::array<unsigned char, 4096> seed{};
    seed[1028] = 4; // Four 1 KiB ext2 blocks.
    seed[1080] = 0x53;
    seed[1081] = 0xef;
    int seedfd = openat(parent, "seed", O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(seedfd >= 0);
    assert(write(seedfd, seed.data(), seed.size()) == (ssize_t)seed.size());
    term::platform::LinuxDataDisk disk, second;
#if defined(PS5TERM_DATA_NO_OPENAT)
    assert(!disk.open(parent, seedfd, seed.size(), "/tmp"));
#endif
    assert(disk.open(parent, seedfd, seed.size(), directory));
    auto device = disk.device();
    assert(device.sectors == 8);
    std::array<unsigned char, 512> block{};
    block[0] = 123;
    block[511] = 77;
    assert(device.write(device.context, 7, block.data(), 1) == 0);
    assert(!second.open(parent, seedfd, seed.size(), directory)); // Lock, even in this process.
    assert(device.write(device.context, 8, block.data(), 1) == -1);
    assert(device.read(device.context, UINT64_MAX, block.data(), 1) == -1);
    disk.close();
    assert(disk.device().sectors == 0);
    assert(second.open(parent, seedfd, seed.size(), directory));
    auto saved = second.device();
    block.fill(0);
    assert(saved.read(saved.context, 7, block.data(), 1) == 0);
    assert(block[0] == 123 && block[511] == 77);
    second.close();
    int grown = openat(parent, "data.ext2", O_RDWR);
    assert(grown >= 0);
    assert(ftruncate(grown, 8192) == 0);
    unsigned char blocks = 8;
    assert(pwrite(grown, &blocks, 1, 1028) == 1);
    close(grown);
    assert(disk.open(parent, seedfd, seed.size(), directory));
    auto larger = disk.device();
    assert(larger.sectors == 16);
    assert(larger.read(larger.context, 7, block.data(), 1) == 0);
    assert(block[0] == 123 && block[511] == 77);
    assert(larger.write(larger.context, 15, block.data(), 1) == 0);
    disk.close();
    grown = openat(parent, "data.ext2", O_RDWR);
    blocks = 9; // Filesystem extends past the backing image.
    assert(pwrite(grown, &blocks, 1, 1028) == 1);
    close(grown);
    assert(!disk.open(parent, seedfd, seed.size(), directory));
    assert(unlinkat(parent, "data.ext2", 0) == 0);
    assert(symlinkat("seed", parent, "data.ext2") == 0);
    assert(!disk.open(parent, seedfd, seed.size(), directory));
    assert(unlinkat(parent, "data.ext2", 0) == 0);
    assert(mkdirat(parent, "data.ext2", 0700) == 0);
    assert(!disk.open(parent, seedfd, seed.size(), directory));
    assert(unlinkat(parent, "data.ext2", AT_REMOVEDIR) == 0);
    int malformed = openat(parent, "data.ext2", O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(malformed >= 0);
    close(malformed);
    assert(!disk.open(parent, seedfd, seed.size(),
                      directory)); // Never reseed a damaged existing file.
    struct stat st
    {
    };
    assert(!fstatat(parent, "data.ext2", &st, 0) && st.st_size == 0);
    assert(unlinkat(parent, "data.ext2", 0) == 0);
    assert(linkat(parent, "seed", parent, "data.ext2", 0) == 0);
    assert(!disk.open(parent, seedfd, seed.size(), directory));
    assert(unlinkat(parent, "data.ext2", 0) == 0);
    assert(!disk.open(parent, seedfd, 513));
    assert(!disk.open(parent, seedfd, 1024u * 1024u * 1024u + 512));
    // A malformed seed must never be published as the active data file.
    unsigned char bad_magic = 0;
    assert(pwrite(seedfd, &bad_magic, 1, 1080) == 1);
    assert(!disk.open(parent, seedfd, seed.size(), directory));
    assert(faccessat(parent, "data.ext2", F_OK, 0) < 0);
    assert(unlinkat(parent, "data.ext2.new", 0) == 0);
    close(seedfd);
    assert(unlinkat(parent, "seed", 0) == 0);
    close(parent);
    assert(!rmdir(directory));
    puts("PASS: persistent file disk seeding/reopen, exact sector IO, locks, bounds, "
         "larger capacity, geometry and symlink/type/size/hardlink rejection");
}
