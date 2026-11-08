// PS5Term GPL-3.0-or-later. Offline worker preserves the original filesystem.
#include "platform/linux_data_disk.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <cerrno>
static void wait(term::platform::LinuxDiskExpansion &job)
{
    for (int i = 0; job.busy() && i < 5000; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(!job.busy());
}
int main()
{
    char path[] = "/tmp/ps5term-grow-XXXXXX";
    assert(mkdtemp(path));
    const std::string dir = path, disk = dir + "/data.ext2", backup = disk + ".backup";
    int fd = open(disk.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && ftruncate(fd, 16 * 1024 * 1024) == 0);
    unsigned char super[60]{};
    super[5] = 0x40;
    super[56] = 0x53;
    super[57] = 0xef;
    assert(pwrite(fd, super, sizeof(super), 1024) == sizeof(super));
    assert(pwrite(fd, "retained", 8, 4096) == 8);
    close(fd);
    term::platform::LinuxDiskExpansion job;
    assert(!job.start(path, 2048));
    assert(job.start(path, 32));
    wait(job);
    assert(job.error() == 0 && job.progress() == 100);
    struct stat info
    {
    };
    assert(stat(disk.c_str(), &info) == 0 && info.st_size == 32 * 1024 * 1024);
    assert(stat(backup.c_str(), &info) == 0 && info.st_size == 16 * 1024 * 1024);
    char value[8];
    fd = open(disk.c_str(), O_RDONLY);
    assert(pread(fd, value, 8, 4096) == 8 && std::string(value, 8) == "retained");
    close(fd);
    // Existing backup must never be overwritten by another expansion.
    assert(job.start(path, 64));
    wait(job);
    assert(job.error() == EEXIST);
    assert(stat(disk.c_str(), &info) == 0 && info.st_size == 32 * 1024 * 1024);
    assert(unlink(backup.c_str()) == 0);
    fd = open(disk.c_str(), O_RDWR);
    assert(fd >= 0);
    assert(flock(fd, LOCK_EX | LOCK_NB) == 0);
    assert(job.start(path, 64));
    wait(job);
    assert(job.error() != 0);
    close(fd);
    assert(access(backup.c_str(), F_OK) < 0);
    // A failure after the backup is written must not leave it behind to block the next try.
    const std::string grow = disk + ".grow", partial = backup + ".new";
    assert(mkdir(grow.c_str(), 0700) == 0);
    assert(job.start(path, 64));
    wait(job);
    assert(job.error() != 0);
    assert(access(backup.c_str(), F_OK) < 0);
    assert(stat(disk.c_str(), &info) == 0 && info.st_size == 32 * 1024 * 1024);
    assert(rmdir(grow.c_str()) == 0);
    // Temporaries left by a crash mid-copy are replaced, not a permanent EEXIST.
    for (const std::string &stale : {grow, partial})
    {
        fd = open(stale.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        assert(fd >= 0 && write(fd, "junk", 4) == 4);
        close(fd);
    }
    assert(job.start(path, 64));
    wait(job);
    assert(job.error() == 0);
    assert(stat(disk.c_str(), &info) == 0 && info.st_size == 64 * 1024 * 1024);
    assert(stat(backup.c_str(), &info) == 0 && info.st_size == 32 * 1024 * 1024);
    assert(access(grow.c_str(), F_OK) < 0 && access(partial.c_str(), F_OK) < 0);
    fd = open(disk.c_str(), O_RDONLY);
    assert(pread(fd, value, 8, 4096) == 8 && std::string(value, 8) == "retained");
    close(fd);
    assert(unlink(backup.c_str()) == 0);
    assert(unlink(disk.c_str()) == 0 && rmdir(path) == 0);
    puts("PASS: asynchronous disk growth, rollback backup, data, locks and overwrite refusal");
}
