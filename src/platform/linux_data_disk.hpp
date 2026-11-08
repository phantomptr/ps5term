// PS5Term GPL-3.0-or-later. File ownership for optional Linux data disk.
#pragma once
#include "../../backend/subsystem/embedded_linux.h"
#include <atomic>
#include <cstddef>
#include <pthread.h>
namespace term::platform
{
class LinuxDataDisk
{
  public:
    LinuxDataDisk() noexcept = default;
    ~LinuxDataDisk()
    {
        close();
    }
    LinuxDataDisk(const LinuxDataDisk &) = delete;
    LinuxDataDisk &operator=(const LinuxDataDisk &) = delete;
    // Trusted, already-open parent directory; seed is a regular ext2 image.
    // Existing sector-aligned ext2 files may be larger than the seed, up to 1 GiB.
    // Never replaces/reinitializes existing data.ext2. Failed initial seeding
    // leaves the partial file for explicit recovery, not silent destruction.
    bool open(int directory_fd, int seed_fd, std::size_t size,
              const char *directory_path = nullptr) noexcept;
    // Call only after joining the guest worker. No race with open/device/close.
    void close() noexcept;
    Ps5LinuxDisk device() noexcept;
    const char *operation() const noexcept
    {
        return operation_.load();
    }
    int error() const noexcept
    {
        return error_.load();
    }

  private:
    int fd_ = -1;
    std::size_t size_ = 0;
    std::atomic<int> error_{0};
    std::atomic<const char *> operation_{"open"};
    int fail(int) noexcept;
    static int read(void *, uint64_t, void *, size_t) noexcept;
    static int write(void *, uint64_t, const void *, size_t) noexcept;
    int transfer(uint64_t, void *, size_t, bool) noexcept;
};
} // namespace term::platform

namespace term::platform
{
// Offline expansion owns its worker and retains a rollback copy. No guest may
// start while busy(). The filesystem is expanded by resize2fs on the next boot.
class LinuxDiskExpansion
{
  public:
    LinuxDiskExpansion() noexcept = default;
    ~LinuxDiskExpansion();
    LinuxDiskExpansion(const LinuxDiskExpansion &) = delete;
    LinuxDiskExpansion &operator=(const LinuxDiskExpansion &) = delete;
    bool start(const char *directory, unsigned mib) noexcept;
    bool busy() const noexcept
    {
        return busy_.load();
    }
    int error() const noexcept
    {
        return error_.load();
    }
    bool requested() const noexcept
    {
        return requested_;
    }
    unsigned progress() const noexcept
    {
        return progress_.load();
    }

  private:
    static void *worker(void *) noexcept;
    void expand() noexcept;
    void join() noexcept;
    std::atomic<bool> busy_{false};
    std::atomic<int> error_{0};
    std::atomic<unsigned> progress_{0};
    char directory_[1024]{};
    unsigned mib_ = 0;
    bool requested_ = false, joinable_ = false;
    pthread_t thread_{};
};
} // namespace term::platform
