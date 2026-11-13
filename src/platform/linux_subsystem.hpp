// PS5Term - GPL-3.0-or-later. Linux guest via TinyEMU RISC-V emulation.
#pragma once
#include <array>
#include "guest_agent.hpp"
#if defined(PS5TERM_LINUX_PERSISTENCE)
#include "linux_data_disk.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
#include "linux_shared_folder.hpp"
#endif
#include <string_view>
#include <cstddef>
#include <cstdint>
#if defined(PS5TERM_LINUX_SUBSYSTEM)
struct Ps5LinuxSession;
#endif
#if defined(PS5TERM_LINUX_NETWORK)
struct Ps5LinuxNat;
#endif
namespace term::platform
{
class LinuxSubsystem
{
  public:
    explicit LinuxSubsystem(const char *directory = "/app0/assets/linux",
                            const char *data_directory = "/data/ps5term/linux",
                            const char *data_root = "/data",
                            const char *console_root = "/") noexcept
        : data_root_(data_root), console_root_(console_root), data_directory_(data_directory),
          directory_(directory)
    {
    }
    static constexpr bool persistence_available() noexcept
    {
#if defined(PS5TERM_LINUX_SUBSYSTEM) && defined(PS5TERM_LINUX_PERSISTENCE)
        return true;
#else
        return false;
#endif
    }
    // What /mnt/ps5 shows, chosen by the user and read each time Linux starts: /data by default,
    // or the app's / for true here. Saved beside the Linux disks.
#if defined(PS5TERM_LINUX_SUBSYSTEM)
    bool mounts_whole_console() const noexcept;
    bool set_mounts_whole_console(bool) noexcept;
#else
    bool mounts_whole_console() const noexcept
    {
        return false;
    }
    bool set_mounts_whole_console(bool) noexcept
    {
        return false;
    }
#endif
    static constexpr bool shared_folder_available() noexcept
    {
#if defined(PS5TERM_LINUX_SUBSYSTEM) && defined(PS5TERM_LINUX_SHARED_FOLDER)
        return true;
#else
        return false;
#endif
    }
    static constexpr bool network_available() noexcept
    {
#if defined(PS5TERM_LINUX_SUBSYSTEM) && defined(PS5TERM_LINUX_NETWORK)
        return true;
#else
        return false;
#endif
    }
    static constexpr bool available() noexcept
    {
#if defined(PS5TERM_LINUX_SUBSYSTEM)
        return true;
#else
        return false;
#endif
    }
    ~LinuxSubsystem();
    LinuxSubsystem(const LinuxSubsystem &) = delete;
    LinuxSubsystem &operator=(const LinuxSubsystem &) = delete;
    bool start(unsigned cols, unsigned rows) noexcept;
    bool stop() noexcept;
    std::string_view status() const noexcept
    {
        return status_.data();
    }
    bool running() const noexcept
    {
        return running_;
    }
    std::size_t read(char *buf, std::size_t size) noexcept;
    std::size_t write(const char *buf, std::size_t size) noexcept;
    bool resize(unsigned cols, unsigned rows) noexcept;
    bool expand_storage(unsigned mib) noexcept
    {
#if defined(PS5TERM_LINUX_PERSISTENCE)
        return !running_ && expansion_.start(data_directory_, mib);
#else
        (void)mib;
        return false;
#endif
    }
    bool storage_busy() const noexcept
    {
#if defined(PS5TERM_LINUX_PERSISTENCE)
        return expansion_.busy();
#else
        return false;
#endif
    }
    unsigned storage_progress() const noexcept
    {
#if defined(PS5TERM_LINUX_PERSISTENCE)
        return expansion_.progress();
#else
        return 0;
#endif
    }
    int storage_error() const noexcept
    {
#if defined(PS5TERM_LINUX_PERSISTENCE)
        return expansion_.error();
#else
        return 0;
#endif
    }
    unsigned storage_mib() const noexcept
    {
#if defined(PS5TERM_LINUX_PERSISTENCE)
        char path[1100];
        struct stat st
        {
        };
        if (std::snprintf(path, sizeof path, "%s/data.ext2", data_directory_) <
                static_cast<int>(sizeof path) &&
            ::stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
            return static_cast<unsigned>(static_cast<std::uint64_t>(st.st_size) >> 20);
#endif
        return 0;
    }
    // The backup a growth left (data.ext2.backup), rounded up; 0 when there is none.
    unsigned backup_mib() const noexcept
    {
#if defined(PS5TERM_LINUX_PERSISTENCE)
        char path[1100];
        struct stat st
        {
        };
        if (std::snprintf(path, sizeof path, "%s/data.ext2.backup", data_directory_) <
                static_cast<int>(sizeof path) &&
            ::stat(path, &st) == 0 && S_ISREG(st.st_mode))
            return static_cast<unsigned>((static_cast<std::uint64_t>(st.st_size) + 0xfffff) >> 20);
#endif
        return 0;
    }
    // Free the PS5's space: the PS5 keeps /data when the app is uninstalled, and other tools
    // cannot remove these owner-only files. Both return 0 or an errno; never while growing.
    int delete_backup() noexcept
    {
        return remove_storage(false);
    }
    // Also the disk itself: every environment and file in /mnt/data. Linux must be stopped;
    // the next start makes a new 16 MiB disk.
    int delete_storage() noexcept
    {
        return remove_storage(true);
    }
    // Independent guest PTYs for SSH sessions. Call pump regularly while running.
    GuestAgent &agent() noexcept
    {
        return agent_;
    }

  private:
    int remove_storage([[maybe_unused]] bool disk) noexcept
    {
#if defined(PS5TERM_LINUX_PERSISTENCE)
        if (expansion_.busy() || (disk && running_))
            return EBUSY;
        int result = 0;
        // Temporaries first (left only by an interrupted growth), the disk last.
        for (const char *name : {"data.ext2.backup.new", "data.ext2.grow", "data.ext2.new",
                                 "data.ext2.backup", "data.ext2"})
        {
            char path[1100];
            if (!disk && std::strcmp(name, "data.ext2") == 0)
                continue;
            if (std::snprintf(path, sizeof path, "%s/%s", data_directory_, name) >=
                static_cast<int>(sizeof path))
                return ENAMETOOLONG;
            if (::unlink(path) != 0 && errno != ENOENT)
                result = errno;
        }
        return result;
#else
        return ENOTSUP;
#endif
    }
    GuestAgent agent_;
#if defined(PS5TERM_LINUX_SUBSYSTEM)
    Ps5LinuxSession *session_ = nullptr;
    uint8_t *bios_ = nullptr;
    uint8_t *kernel_ = nullptr;
    uint8_t *disk_ = nullptr;
    std::size_t bios_size_ = 0, kernel_size_ = 0, disk_size_ = 0;
#endif
#if defined(PS5TERM_LINUX_PERSISTENCE)
    // Retained with a worker on join failure, including during destruction;
    // callbacks must never point into the destroyed frontend object.
    LinuxDiskExpansion expansion_;
    LinuxDataDisk *data_disk_ = nullptr;
    bool open_data_disk() noexcept;
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
    LinuxSharedFolder *shared_folder_ = nullptr;
    void open_shared_folder() noexcept;
    int shared_error_ = 0;
#endif
#if defined(PS5TERM_LINUX_NETWORK)
    // Same join-before-free lifetime as the images and data disk.
    Ps5LinuxNat *network_ = nullptr;
    std::array<char, 16> resolver_text_{};
#endif
    [[maybe_unused]] const char *data_root_;
    [[maybe_unused]] const char *console_root_;
    [[maybe_unused]] const char *data_directory_;
    [[maybe_unused]] const char *directory_; // Borrowed immutable path, outlives this loader.
    std::array<char, 160> status_{"Linux stopped"};
#if defined(PS5TERM_LINUX_SUBSYSTEM)
    std::array<uint8_t, 64> rng_seed_{}; // Read by the worker during boot.
#endif
    bool running_ = false;
#if defined(PS5TERM_LINUX_SUBSYSTEM)
    bool load_images() noexcept;
    void free_images() noexcept;
#endif
};
} // namespace term::platform
