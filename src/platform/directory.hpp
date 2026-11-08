// PS5Term - owned directory iteration. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#if !defined(__PROSPERO__)
#include <dirent.h>
#endif
namespace term::platform
{
struct DirectoryRecord
{
    bool valid = false;
    std::size_t bytes = 0;
    std::string_view name{};
};
// Decoder for the public SDK's FreeBSD 11 directory ABI. No native pointers or
// unaligned struct casts; names reference the caller's buffer until next read.
DirectoryRecord decode_directory_record(std::span<const char>) noexcept;
enum class DirectoryStep
{
    entry,
    end,
    failed
};
class DirectoryCursor
{
  public:
    ~DirectoryCursor();
    DirectoryCursor() noexcept = default;
    DirectoryCursor(const DirectoryCursor &) = delete;
    DirectoryCursor &operator=(const DirectoryCursor &) = delete;
    bool open(const char *path, bool no_follow = false) noexcept;
    DirectoryStep next(std::string_view &name) noexcept;
    void close() noexcept;

  private:
#if defined(__PROSPERO__)
    int descriptor_ = -1;
    // app0 directory blocks require at least 64 KiB on tested consoles.
    // Keep a fixed bound; larger filesystem blocks fail explicitly.
    std::array<char, 65536> buffer_{};
    std::size_t used_ = 0, offset_ = 0;
    long base_ = 0;
#else
    DIR *directory_ = nullptr;
#endif
};
} // namespace term::platform
