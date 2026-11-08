// PS5Term - GPL-3.0-or-later.
#include "disk_usage.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

namespace term::platform
{
struct DiskUsage::Frame
{
    DirectoryCursor cursor;
    std::array<char, 1024> path{};
};
DiskUsage::DiskUsage(Metadata metadata) noexcept : metadata_(metadata)
{
}
DiskUsage::~DiskUsage() = default;
void DiskUsage::release() noexcept
{
    for (auto &frame : frames_)
        frame.reset();
    depth_ = 0;
}
void DiskUsage::fail(int error) noexcept
{
    error_ = error ? error : EIO;
    done_ = true;
    release();
}
bool DiskUsage::start(const char *path) noexcept
{
    release();
    current_ = {};
    blocks_ = 0;
    visited_ = link_count_ = 0;
    error_ = 0;
    done_ = false;
    if (!metadata_ || !path || !*path)
    {
        fail(EINVAL);
        return false;
    }
    const auto path_length = std::strlen(path);
    if (path_length >= current_.size())
    {
        fail(ENAMETOOLONG);
        return false;
    }
    std::memcpy(current_.data(), path, path_length + 1);
    auto length = std::strlen(current_.data());
    while (length > 1 && current_[length - 1] == '/')
        current_[--length] = 0;
    visit();
    if (!depth_ && !error_)
        done_ = true;
    return !error_;
}
void DiskUsage::visit() noexcept
{
    if (++visited_ > entry_limit)
    {
        fail(E2BIG);
        return;
    }
    struct stat st
    {
    };
    if (metadata_(current_.data(), &st) < 0)
    {
        fail(errno);
        return;
    }
    if (st.st_blocks < 0)
    {
        fail(EIO);
        return;
    }
    if (!S_ISDIR(st.st_mode) && st.st_nlink > 1)
    {
        for (unsigned i = 0; i < link_count_; ++i)
            if (links_[i].device == st.st_dev && links_[i].inode == st.st_ino)
                return;
        if (link_count_ == link_limit)
        {
            fail(E2BIG);
            return;
        }
        links_[link_count_++] = {st.st_dev, st.st_ino};
    }
    const auto blocks = static_cast<std::uint64_t>(st.st_blocks);
    if (blocks > std::numeric_limits<std::uint64_t>::max() - blocks_)
    {
        fail(EOVERFLOW);
        return;
    }
    blocks_ += blocks;
    if (!S_ISDIR(st.st_mode))
        return; // Includes links, FIFOs and devices: metadata only.
    if (depth_ == depth_limit)
    {
        fail(ELOOP);
        return;
    }
    std::unique_ptr<Frame> frame(new (std::nothrow) Frame);
    if (!frame)
    {
        fail(ENOMEM);
        return;
    }
    frame->path = current_;
    // A directory swapped to a link between metadata and open must not be followed.
    if (!frame->cursor.open(current_.data(), true))
    {
        fail(errno);
        return;
    }
    frames_[depth_++] = std::move(frame);
}
void DiskUsage::tick() noexcept
{
    for (unsigned budget = 0; budget < 32 && !done_; ++budget)
    {
        if (!depth_)
        {
            done_ = true;
            break;
        }
        auto &frame = *frames_[depth_ - 1];
        std::string_view name;
        const auto step = frame.cursor.next(name);
        if (step == DirectoryStep::failed)
        {
            current_ = frame.path;
            fail(errno);
            break;
        }
        if (step == DirectoryStep::end)
        {
            frames_[--depth_].reset();
            if (!depth_)
                done_ = true;
            continue;
        }
        if (name == "." || name == "..")
            continue;
        // Reject malformed cursor records; these cannot be single directory names.
        if (name.empty() || name.find('/') != name.npos || name.find('\0') != name.npos)
        {
            current_ = frame.path;
            fail(EIO);
            break;
        }
        const int n = std::snprintf(current_.data(), current_.size(), "%s/%.*s", frame.path.data(),
                                    static_cast<int>(name.size()), name.data());
        if (n < 0 || static_cast<std::size_t>(n) >= current_.size())
        {
            fail(ENAMETOOLONG);
            break;
        }
        visit();
    }
}
} // namespace term::platform
