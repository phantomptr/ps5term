// PS5Term - bounded, cancellable allocated-storage traversal. GPL-3.0-or-later.
#pragma once
#include "directory.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <sys/stat.h>

namespace term::platform
{
class DiskUsage
{
  public:
    using Metadata = int (*)(const char *, struct stat *);
    explicit DiskUsage(Metadata metadata) noexcept;
    ~DiskUsage();
    DiskUsage(const DiskUsage &) = delete;
    DiskUsage &operator=(const DiskUsage &) = delete;
    bool start(const char *) noexcept;
    void tick() noexcept;
    bool done() const noexcept
    {
        return done_;
    }
    int error() const noexcept
    {
        return error_;
    }
    const char *error_path() const noexcept
    {
        return current_.data();
    }
    std::uint64_t blocks() const noexcept
    {
        return blocks_;
    } // POSIX 512-byte blocks.
    static constexpr unsigned depth_limit = 32, entry_limit = 100000, link_limit = 2048;

  private:
    struct Frame;
    struct Identity
    {
        dev_t device{};
        ino_t inode{};
    };
    Metadata metadata_;
    std::array<std::unique_ptr<Frame>, depth_limit> frames_{};
    std::array<Identity, link_limit> links_{};
    std::array<char, 1024> current_{};
    std::uint64_t blocks_ = 0;
    unsigned depth_ = 0, visited_ = 0, link_count_ = 0;
    int error_ = 0;
    bool done_ = true;
    void visit() noexcept;
    void fail(int) noexcept;
    void release() noexcept;
};
} // namespace term::platform
