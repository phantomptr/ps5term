// PS5Term GPL-3.0-or-later.
#pragma once
#include "directory.hpp"
#include "shared_folder.h"
#include <cstddef>
#include <cstdint>
#include <array>
namespace term::platform
{
// Exposes one native directory, /data or the app's /, to the guest. This is the user's
// chosen reach, not a sandbox: guest root can do what this app can there. Two things stay out
// of it: PS5Term's SSH identity and its Linux disks, and any file that is not a regular one,
// because opening a native device or FIFO could stall the emulator's worker.
class LinuxSharedFolder
{
  public:
    LinuxSharedFolder() noexcept = default;
    ~LinuxSharedFolder()
    {
        close();
    }
    LinuxSharedFolder(const LinuxSharedFolder &) = delete;
    LinuxSharedFolder &operator=(const LinuxSharedFolder &) = delete;
    bool open(const char *path) noexcept;
    void close() noexcept;
    Ps5LinuxShare device() noexcept;
    int error() const noexcept
    {
        return error_;
    }
    static constexpr std::size_t open_files = 64, directory_readers = 16;

  private:
    struct File
    {
        int fd = -1;
        bool written = false;
    };
    struct Reader
    {
        DirectoryCursor *cursor = nullptr;
        bool active = false, ended = false, has_name = false;
        unsigned key = 0;
        std::uint64_t consumed = 0, used = 0;
        std::array<char, PS5_SHARE_PATH> path{};
        std::array<char, PS5_SHARE_NAME> name{};
    };
    std::array<File, open_files> files_{};
    std::array<Reader, directory_readers> readers_{};
    std::array<char, 512> root_{};
    std::uint64_t clock_ = 0;
    int error_ = 0;
    bool opened_ = false;
    bool native_path(const char *, char *, std::size_t) const noexcept;
    File *file(unsigned) noexcept;
    Reader *reader(unsigned, const char *) noexcept;
    static int stat(void *, const char *, Ps5ShareStat *) noexcept;
    static int fstat(void *, unsigned, Ps5ShareStat *) noexcept;
    static int open_file(void *, const char *, int, unsigned) noexcept;
    static void close_file(void *, unsigned) noexcept;
    static int read(void *, unsigned, std::uint64_t, void *, std::size_t) noexcept;
    static int write(void *, unsigned, std::uint64_t, const void *, std::size_t) noexcept;
    static int resize(void *, unsigned, const char *, std::uint64_t) noexcept;
    static int chmod(void *, const char *, unsigned) noexcept;
    static int times(void *, const char *, std::uint64_t, std::uint32_t, std::uint64_t,
                     std::uint32_t, int) noexcept;
    static int mkdir(void *, const char *, unsigned) noexcept;
    static int remove(void *, const char *) noexcept;
    static int rename(void *, const char *, const char *) noexcept;
    static int readlink(void *, const char *, char *, std::size_t) noexcept;
    static int list(void *, unsigned, const char *, std::uint64_t, char *, std::size_t) noexcept;
    static void list_close(void *, unsigned) noexcept;
    static int space(void *, Ps5ShareSpace *) noexcept;
};
} // namespace term::platform
