// PS5Term GPL-3.0-or-later.
#include "linux_data_disk.hpp"
#include <array>
#include <new>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/file.h>
namespace term::platform
{
namespace
{
bool sync_fd(int fd) noexcept
{
    int result;
    do
    {
        result = ::fsync(fd);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}
bool valid_links(nlink_t count) noexcept
{
#if defined(__PROSPERO__)
    // /data reports zero on phat 13.60, as in owned SSH's native stat adapter.
    // This is unavailable metadata, not proof of single-link semantics.
    return count <= 1;
#else
    return count == 1;
#endif
}
int flags() noexcept
{
    int value = O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    return value;
}
} // namespace
int LinuxDataDisk::fail(int code) noexcept
{
    error_.store(code ? code : EIO);
    return -1;
}
void LinuxDataDisk::close() noexcept
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    size_ = 0;
}
bool LinuxDataDisk::open(int directory, int seed, std::size_t size,
                         const char *directory_path) noexcept
{
    close();
    error_.store(0);
    operation_.store("validate directory");
    if (size < 2048 || size % 512 || size > data_disk_max_bytes)
    {
        fail(EINVAL);
        return false;
    }
    struct stat st
    {
    };
    if (::fstat(directory, &st) < 0 || !S_ISDIR(st.st_mode))
    {
        fail(ENOTDIR);
        return false;
    }
#if defined(__PROSPERO__) || defined(PS5TERM_DATA_NO_OPENAT)
    // Sony's app context rejects the SDK inline openat syscall; libkernel's
    // _openat returns EPERM. Open the fixed app-private path after comparing its
    // parent to the borrowed directory. This is not a hostile-process sandbox.
    char path[1024];
    if (!directory_path)
    {
        fail(EINVAL);
        return false;
    }
    int n = std::snprintf(path, sizeof(path), "%s/data.ext2", directory_path);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(path))
    {
        fail(ENAMETOOLONG);
        return false;
    }
    int check = ::open(directory_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat actual
    {
    };
    bool same = check >= 0 && !::fstat(check, &actual) && actual.st_dev == st.st_dev &&
                actual.st_ino == st.st_ino;
    if (check >= 0)
        ::close(check);
    if (!same)
    {
        fail(EINVAL);
        return false;
    }
    char temporary[1024];
    if (std::snprintf(temporary, sizeof(temporary), "%s/data.ext2.new", directory_path) >=
        static_cast<int>(sizeof(temporary)))
    {
        fail(ENAMETOOLONG);
        return false;
    }
    const auto open_data = [&](int mode) { return ::open(path, mode, 0600); };
    const auto open_new = [&](int mode) { return ::open(temporary, mode, 0600); };
    const auto publish = [&]() { return ::rename(temporary, path); };
#else
    (void)directory_path;
    const auto open_data = [&](int mode) { return ::openat(directory, "data.ext2", mode, 0600); };
    const auto open_new = [&](int mode)
    { return ::openat(directory, "data.ext2.new", mode, 0600); };
    const auto publish = [&]()
    { return ::renameat(directory, "data.ext2.new", directory, "data.ext2"); };
#endif
    operation_.store("open data file");
    bool created = false;
    fd_ = open_data(flags());
    if (fd_ < 0 && errno == ENOENT)
    {
        // A wrong seed must leave nothing behind that blocks a later start.
        operation_.store("check seed");
        struct stat source
        {
        };
        if (::fstat(seed, &source) < 0 || !S_ISREG(source.st_mode) ||
            source.st_size != static_cast<off_t>(size))
        {
            fail(EINVAL);
            return false;
        }
        // data.ext2 only ever appears complete; a leftover .new from an interrupted seed is
        // rewritten.
        fd_ = open_new(flags() | O_CREAT | O_TRUNC);
        created = fd_ >= 0;
    }
    if (fd_ < 0)
    {
        fail(errno);
        return false;
    }
    operation_.store("lock data file");
    if (::flock(fd_, LOCK_EX | LOCK_NB) < 0)
    {
        fail(errno);
        close();
        return false;
    }
    operation_.store("check data file");
    if (::fstat(fd_, &st) < 0 || !S_ISREG(st.st_mode) || !valid_links(st.st_nlink) ||
        (!created &&
         (st.st_size < static_cast<off_t>(size) ||
          static_cast<std::uint64_t>(st.st_size) > data_disk_max_bytes || st.st_size % 512)))
    {
        fail(EINVAL);
        close();
        return false;
    }
    if (created)
    {
        operation_.store("copy seed");
        std::array<unsigned char, 16384> buffer{};
        std::size_t offset = 0;
        while (offset < size)
        {
            auto count = size - offset < buffer.size() ? size - offset : buffer.size();
            ssize_t n = ::pread(seed, buffer.data(), count, static_cast<off_t>(offset));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
            {
                fail(n < 0 ? errno : EIO);
                close();
                return false;
            }
            std::size_t done = 0;
            while (done < static_cast<std::size_t>(n))
            {
                auto wrote = ::pwrite(fd_, buffer.data() + done, static_cast<std::size_t>(n) - done,
                                      static_cast<off_t>(offset + done));
                if (wrote < 0 && errno == EINTR)
                    continue;
                if (wrote <= 0)
                {
                    fail(wrote < 0 ? errno : EIO);
                    close();
                    return false;
                }
                done += static_cast<std::size_t>(wrote);
            }
            offset += static_cast<std::size_t>(n);
        }
    }
    operation_.store("check filesystem");
    unsigned char super[60];
    if (::pread(fd_, super, sizeof(super), 1024) != sizeof(super) || super[56] != 0x53 ||
        super[57] != 0xef)
    {
        fail(EINVAL);
        close();
        return false;
    }
    const auto le32 = [](const unsigned char *p) -> uint32_t
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
               (uint32_t(p[3]) << 24);
    };
    const uint32_t blocks = le32(super + 4), log_block_size = le32(super + 24);
    const uint64_t capacity = created ? size : static_cast<uint64_t>(st.st_size);
    if (!blocks || log_block_size > 2 ||
        uint64_t(blocks) * (uint64_t(1024) << log_block_size) > capacity)
    {
        fail(EINVAL);
        close();
        return false;
    }
    if (created)
    {
        operation_.store("sync new data");
        if (!sync_fd(fd_) || publish() < 0 || !sync_fd(directory))
        {
            fail(errno);
            close();
            return false;
        }
    }
    size_ = static_cast<std::size_t>(capacity);
    return true;
}
Ps5LinuxDisk LinuxDataDisk::device() noexcept
{
    return fd_ < 0 ? Ps5LinuxDisk{} : Ps5LinuxDisk{this, size_ / 512, read, write};
}
int LinuxDataDisk::read(void *p, uint64_t sector, void *bytes, size_t count) noexcept
{
    return static_cast<LinuxDataDisk *>(p)->transfer(sector, bytes, count, false);
}
int LinuxDataDisk::write(void *p, uint64_t sector, const void *bytes, size_t count) noexcept
{
    return static_cast<LinuxDataDisk *>(p)->transfer(sector, const_cast<void *>(bytes), count,
                                                     true);
}
int LinuxDataDisk::transfer(uint64_t sector, void *bytes, size_t count, bool writing) noexcept
{
    if (fd_ < 0 || error_.load())
        return -1;
    if (sector > size_ / 512 || count > size_ / 512 - sector || (count && !bytes))
        return fail(EINVAL);
    operation_.store(writing ? "write data" : "read data");
    auto *p = static_cast<unsigned char *>(bytes);
    std::size_t total = count * 512, done = 0;
    while (done < total)
    {
        off_t offset = static_cast<off_t>(sector * 512 + done);
        ssize_t n = writing ? ::pwrite(fd_, p + done, total - done, offset)
                            : ::pread(fd_, p + done, total - done, offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return fail(n < 0 ? errno : EIO);
        done += static_cast<std::size_t>(n);
    }
    if (writing && count && !sync_fd(fd_))
        return fail(errno);
    return 0;
}
} // namespace term::platform

namespace term::platform
{
LinuxDiskExpansion::~LinuxDiskExpansion()
{
    join();
}
void LinuxDiskExpansion::join() noexcept
{
    if (joinable_)
    {
        pthread_join(thread_, nullptr);
        joinable_ = false;
    }
}
bool LinuxDiskExpansion::start(const char *directory, unsigned mib) noexcept
{
    if (busy() || !directory || mib < 16 || mib > data_disk_max_bytes / (1024 * 1024) ||
        std::strlen(directory) >= sizeof(directory_) - 32)
        return false;
    join();
    std::snprintf(directory_, sizeof(directory_), "%s", directory);
    mib_ = mib;
    requested_ = true;
    error_.store(0);
    progress_.store(0);
    busy_.store(true);
    pthread_attr_t attr;
    int result = pthread_attr_init(&attr);
    if (!result)
    {
        result = pthread_attr_setstacksize(&attr, 256 * 1024);
        if (!result)
            result = pthread_create(&thread_, &attr, worker, this);
        pthread_attr_destroy(&attr);
    }
    if (result)
    {
        error_.store(result);
        busy_.store(false);
        return false;
    }
    joinable_ = true;
    return true;
}
void *LinuxDiskExpansion::worker(void *opaque) noexcept
{
    auto *job = static_cast<LinuxDiskExpansion *>(opaque);
    job->expand();
    job->busy_.store(false);
    return nullptr;
}
void LinuxDiskExpansion::expand() noexcept
{
    char source[1100], backup[1100], temporary_backup[1100], stale[1100];
    std::snprintf(source, sizeof(source), "%s/data.ext2", directory_);
    std::snprintf(backup, sizeof(backup), "%s/data.ext2.backup", directory_);
    std::snprintf(temporary_backup, sizeof(temporary_backup), "%s/data.ext2.backup.new",
                  directory_);
    // Older versions grew a copy here; a crash could leave it behind.
    std::snprintf(stale, sizeof(stale), "%s/data.ext2.grow", directory_);
    int parent = -1, original = -1, out = -1;
    bool own_backup = false;
    // Large transfers: small ones are slow on the console. Off the 256 KiB worker stack.
    constexpr std::size_t chunk = 1024 * 1024;
    auto *bytes = new (std::nothrow) unsigned char[chunk];
    const auto work = [&]() -> int
    {
        if (!bytes)
            return ENOMEM;
        parent = ::open(directory_, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (parent < 0)
            return errno;
        original = ::open(source, flags());
        if (original < 0)
            return errno;
        if (::flock(original, LOCK_EX | LOCK_NB))
            return errno;
        struct stat st
        {
        };
        if (::fstat(original, &st))
            return errno;
        const uint64_t target = uint64_t(mib_) * 1024 * 1024;
        if (!S_ISREG(st.st_mode) || !valid_links(st.st_nlink) || st.st_size < 16 * 1024 * 1024 ||
            st.st_size % 512 || static_cast<uint64_t>(st.st_size) >= target)
            return EINVAL;
        unsigned char super[60];
        if (::pread(original, super, sizeof(super), 1024) != sizeof(super) || super[56] != 0x53 ||
            super[57] != 0xef)
            return EINVAL;
        const auto le32 = [](const unsigned char *p) -> uint32_t {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
                   uint32_t(p[3]) << 24;
        };
        const auto blocks = le32(super + 4), shift = le32(super + 24);
        if (!blocks || shift > 2 ||
            uint64_t(blocks) * (uint64_t(1024) << shift) > static_cast<uint64_t>(st.st_size))
            return EINVAL;
        (void)::unlink(stale);
        (void)::unlink(temporary_backup); // A crash mid-copy can leave an incomplete one.
        // The work is the bytes written: the backup copy, then the new space.
        uint64_t written = 0;
        const auto step = [&](uint64_t count)
        {
            written += count;
            progress_.store(static_cast<unsigned>(written * 99 / target));
        };
        // If an earlier growth never finished (the file is larger than its filesystem: the
        // guest's resize did not complete), an existing backup is the last copy known to be
        // good. Keep it rather than replace it with a disk that may be damaged.
        const bool resize_pending =
            uint64_t(blocks) * (uint64_t(1024) << shift) < static_cast<uint64_t>(st.st_size);
        const int existing = ::open(backup, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (existing >= 0)
            ::close(existing);
        if (existing >= 0 && resize_pending)
            step(static_cast<uint64_t>(st.st_size));
        else
        {
            // Otherwise the newest copy is the useful rollback; keeping both would need
            // twice the PS5 space. The old backup is replaced only once the new one is whole.
            out = ::open(temporary_backup, flags() | O_CREAT | O_EXCL, 0600);
            if (out < 0)
                return errno;
            own_backup = true;
            off_t offset = 0;
            while (offset < st.st_size)
            {
                const size_t count = static_cast<size_t>(
                    std::min<off_t>(static_cast<off_t>(chunk), st.st_size - offset));
                ssize_t got = ::pread(original, bytes, count, offset);
                if (got < 0 && errno == EINTR)
                    continue;
                if (got <= 0)
                    return got ? errno : EIO;
                ssize_t done = 0;
                while (done < got)
                {
                    ssize_t wrote = ::pwrite(out, bytes + done, got - done, offset + done);
                    if (wrote < 0 && errno == EINTR)
                        continue;
                    if (wrote <= 0)
                        return wrote ? errno : EIO;
                    done += wrote;
                }
                offset += got;
                step(static_cast<uint64_t>(got));
            }
            if (!sync_fd(out))
                return errno;
            if (::rename(temporary_backup, backup) || !sync_fd(parent))
                return errno;
            own_backup = false;
        }
        // Growing only adds zeroed space at the end; the guest's resize2fs uses it at the
        // next boot. The zeroes are written, not left as a hole: a PS5 drive without room
        // fails here, cleanly, instead of under a mounted filesystem later. A failure puts
        // the disk back at its old size, still usable.
        std::memset(bytes, 0, chunk);
        uint64_t end = static_cast<uint64_t>(st.st_size);
        while (end < target)
        {
            const size_t count = static_cast<size_t>(std::min<uint64_t>(chunk, target - end));
            const ssize_t wrote = ::pwrite(original, bytes, count, static_cast<off_t>(end));
            if (wrote < 0 && errno == EINTR)
                continue;
            if (wrote <= 0)
            {
                const int reason = wrote ? errno : EIO;
                (void)::ftruncate(original, st.st_size);
                return reason;
            }
            end += static_cast<uint64_t>(wrote);
            step(static_cast<uint64_t>(wrote));
        }
        if (!sync_fd(original))
        {
            const int reason = errno;
            (void)::ftruncate(original, st.st_size);
            return reason;
        }
        progress_.store(100);
        return 0;
    };
    error_.store(work());
    delete[] bytes;
    if (out >= 0)
        ::close(out);
    if (original >= 0)
        ::close(original);
    if (own_backup)
        ::unlink(temporary_backup);
    if (parent >= 0)
        ::close(parent);
}
} // namespace term::platform
