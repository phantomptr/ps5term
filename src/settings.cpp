// PS5Term - persistent SSH preference. SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace term
{
unsigned Settings::text_style() const noexcept
{
    char path[1024], bytes[3]{};
    if (std::snprintf(path, sizeof(path), "%s/text.size", directory_) >=
        static_cast<int>(sizeof(path)))
        return 2;
    int fd = ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return 2;
    const auto count = ::read(fd, bytes, sizeof(bytes));
    ::close(fd);
    return count == 2 && bytes[0] >= '1' && bytes[0] <= '3' && bytes[1] == '\n'
               ? static_cast<unsigned>(bytes[0] - '0')
               : 2;
}
bool Settings::set_text_style(unsigned style) noexcept
{
    if (style < 1 || style > 3)
        return false;
    if (::mkdir(directory_, 0700) < 0 && errno != EEXIST)
        return false;
    char path[1024], temporary[1024];
    if (std::snprintf(path, sizeof(path), "%s/text.size", directory_) >=
            static_cast<int>(sizeof(path)) ||
        std::snprintf(temporary, sizeof(temporary), "%s/text.size.new", directory_) >=
            static_cast<int>(sizeof(temporary)))
        return false;
    int fd =
        ::open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const char bytes[] = {static_cast<char>('0' + style), '\n'};
    bool ok = ::write(fd, bytes, sizeof(bytes)) == sizeof(bytes) && ::fsync(fd) == 0;
    if (::close(fd))
        ok = false;
    if (ok)
        ok = ::rename(temporary, path) == 0;
    if (!ok)
        ::unlink(temporary);
    return ok;
}
// The language choice: a number (0 = Auto) in "language", written the same atomic way.
unsigned Settings::language() const noexcept
{
    char path[1024], bytes[4]{};
    if (std::snprintf(path, sizeof(path), "%s/language", directory_) >=
        static_cast<int>(sizeof(path)))
        return 0;
    int fd = ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return 0;
    const auto count = ::read(fd, bytes, sizeof(bytes));
    ::close(fd);
    unsigned value = 0;
    for (long i = 0; i < count; ++i)
    {
        if (bytes[i] == '\n' && i > 0)
            return value;
        if (bytes[i] < '0' || bytes[i] > '9')
            return 0;
        value = value * 10 + static_cast<unsigned>(bytes[i] - '0');
    }
    return 0;
}
bool Settings::set_language(unsigned language) noexcept
{
    if (language > 99)
        return false;
    if (::mkdir(directory_, 0700) < 0 && errno != EEXIST)
        return false;
    char path[1024], temporary[1024];
    if (std::snprintf(path, sizeof(path), "%s/language", directory_) >=
            static_cast<int>(sizeof(path)) ||
        std::snprintf(temporary, sizeof(temporary), "%s/language.new", directory_) >=
            static_cast<int>(sizeof(temporary)))
        return false;
    int fd =
        ::open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    char bytes[4];
    const int size = std::snprintf(bytes, sizeof(bytes), "%u\n", language);
    bool ok = ::write(fd, bytes, static_cast<std::size_t>(size)) == size && ::fsync(fd) == 0;
    if (::close(fd))
        ok = false;
    if (ok)
        ok = ::rename(temporary, path) == 0;
    if (!ok)
        ::unlink(temporary);
    return ok;
}
bool Settings::ssh_enabled() const noexcept
{
    int file = ::open(flag_, O_RDONLY);
    if (file < 0)
        return false;
    ::close(file);
    return true;
}
bool Settings::read_ssh_password(char *output, std::size_t capacity, const char *path) noexcept
{
    if (!output || capacity < 2)
        return false;
    output[0] = 0;
    int file = ::open(path, O_RDONLY);
    if (file < 0)
        return false;
    char buffer[256]{};
    auto length = ::read(file, buffer, sizeof(buffer));
    ::close(file);
    std::size_t count = 0;
    while (length > 0 && count < static_cast<std::size_t>(length) && buffer[count] != '\r' &&
           buffer[count] != '\n' && buffer[count] != 0)
        ++count;
#if !defined(PS5TERM_OWNED_SSH)
    while (count && (buffer[count - 1] == ' ' || buffer[count - 1] == '\t'))
        --count;
#endif
    bool valid = count > 0 && count < capacity && count < sizeof(buffer);
    for (std::size_t i = 0; valid && i < count; ++i)
        valid = buffer[i] >= 32 && buffer[i] <= 126;
    if (valid)
    {
        for (std::size_t i = 0; i < count; ++i)
            output[i] = buffer[i];
        output[count] = 0;
    }
    volatile char *wipe = buffer;
    for (std::size_t i = 0; i < sizeof(buffer); ++i)
        wipe[i] = 0;
    return valid;
}
bool Settings::set_ssh_enabled(bool enabled) noexcept
{
    if (!enabled)
        return ::unlink(flag_) == 0 || errno == ENOENT;
    if (::mkdir(directory_, 0700) < 0 && errno != EEXIST)
        return false;
    int file = ::open(flag_, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file < 0)
        return false;
    bool result = ::write(file, "1\n", 2) == 2;
    if (::close(file) < 0)
        result = false;
    if (!result)
        ::unlink(flag_);
    return result;
}
} // namespace term
