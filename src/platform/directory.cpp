// PS5Term - GPL-3.0-or-later.
#include "directory.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(__PROSPERO__)
#include <dirent.h>
#endif
namespace term::platform
{
DirectoryRecord decode_directory_record(std::span<const char> bytes) noexcept
{
    if (bytes.size() < 8)
        return {};
    const auto record = static_cast<unsigned char>(bytes[4]) |
                        (static_cast<unsigned>(static_cast<unsigned char>(bytes[5])) << 8);
    const auto length = static_cast<unsigned char>(bytes[7]);
    if (record < 12 || record % 4 || record > bytes.size() || 8U + length >= record ||
        bytes[8 + length] != 0 || std::memchr(bytes.data() + 8, 0, length))
        return {};
    return {true, record, {bytes.data() + 8, length}};
}
DirectoryCursor::~DirectoryCursor()
{
    close();
}
void DirectoryCursor::close() noexcept
{
#if defined(__PROSPERO__)
    if (descriptor_ >= 0)
        ::close(descriptor_);
    descriptor_ = -1;
    used_ = offset_ = 0;
    base_ = 0;
#else
    if (directory_)
        closedir(directory_);
    directory_ = nullptr;
#endif
}
bool DirectoryCursor::open(const char *path, bool no_follow) noexcept
{
    close();
#if defined(__PROSPERO__)
    // Avoid libSceLibcInternal's directory implementation. The supported
    // public libkernel wrapper supplies POSIX errno translation.
    descriptor_ = ::open(path, O_RDONLY | O_DIRECTORY | (no_follow ? O_NOFOLLOW : 0));
    return descriptor_ >= 0;
#else
    if (no_follow)
    {
        const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            return false;
        directory_ = fdopendir(fd);
        if (!directory_)
        {
            const int saved = errno;
            ::close(fd);
            errno = saved;
        }
    }
    else
        directory_ = opendir(path);
    return directory_ != nullptr;
#endif
}
DirectoryStep DirectoryCursor::next(std::string_view &name) noexcept
{
    name = {};
#if defined(__PROSPERO__)
    if (descriptor_ < 0)
    {
        errno = EBADF;
        return DirectoryStep::failed;
    }
    if (offset_ == used_)
    {
        const auto count = getdirentries(descriptor_, buffer_.data(), buffer_.size(), &base_);
        if (count < 0)
            return DirectoryStep::failed;
        if (!count)
            return DirectoryStep::end;
        if (static_cast<std::size_t>(count) > buffer_.size())
        {
            errno = EIO;
            return DirectoryStep::failed;
        }
        used_ = count;
        offset_ = 0;
    }
    const auto record = decode_directory_record({buffer_.data() + offset_, used_ - offset_});
    if (!record.valid)
    {
        errno = EIO;
        return DirectoryStep::failed;
    }
    offset_ += record.bytes;
    name = record.name;
    return DirectoryStep::entry;
#else
    if (!directory_)
    {
        errno = EBADF;
        return DirectoryStep::failed;
    }
    errno = 0;
    const auto *record = readdir(directory_);
    if (!record)
        return errno ? DirectoryStep::failed : DirectoryStep::end;
    name = record->d_name;
    return DirectoryStep::entry;
#endif
}
} // namespace term::platform
