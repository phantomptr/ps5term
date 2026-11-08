// PS5Term GPL-3.0-or-later.
#include "linux_shared_folder.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <string_view>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#if __has_include(<sys/statfs.h>)
#include <sys/statfs.h>
#endif
#if defined(__PROSPERO__)
// The SDK's libc.a implements statfs and readlink as raw syscalls, which end the app on the console
// (tools/build.sh rejects them). libkernel exports only this one to apps, and at firmware 13.60
// it refuses with EPERM. That is harmless: the guest then sees a filesystem of unknown size.
extern "C" int _fstatfs(int, struct statfs *);
#endif
namespace term::platform
{
namespace
{
// Native errno numbers differ from the Linux numbers the guest expects.
int mapped_error(int e) noexcept
{
    switch (e)
    {
    case EPERM:
        return -1;
    case ENOENT:
        return -2;
    case EBADF:
        return -9;
    case ENOMEM:
        return -12;
    case EACCES:
        return -13;
    case EBUSY:
        return -16;
    case EEXIST:
        return -17;
    case EXDEV:
        return -18;
    case ENOTDIR:
        return -20;
    case EISDIR:
        return -21;
    case EINVAL:
        return -22;
    case ENFILE:
        return -23;
    case EMFILE:
        return -24;
    case EFBIG:
        return -27;
    case ENOSPC:
        return -28;
    case EROFS:
        return -30;
    case ENAMETOOLONG:
        return -36;
    case ENOTEMPTY:
        return -39;
    case ELOOP:
        return -40;
    default:
        return -5;
    }
}
// Linux and native mode bits share these values, but the type constants are stated explicitly.
std::uint32_t linux_mode(mode_t mode) noexcept
{
    std::uint32_t type = 0100000;
    if (S_ISDIR(mode))
        type = 0040000;
    else if (S_ISLNK(mode))
        type = 0120000;
    else if (S_ISCHR(mode))
        type = 0020000;
    else if (S_ISBLK(mode))
        type = 0060000;
    else if (S_ISFIFO(mode))
        type = 0010000;
    else if (S_ISSOCK(mode))
        type = 0140000;
    return type | (static_cast<std::uint32_t>(mode) & 07777);
}
void fill(const struct stat &s, Ps5ShareStat &out) noexcept
{
    out = {};
    out.device = static_cast<std::uint64_t>(s.st_dev);
    out.inode = static_cast<std::uint64_t>(s.st_ino);
    out.size = s.st_size < 0 ? 0 : static_cast<std::uint64_t>(s.st_size);
    out.blocks = s.st_blocks < 0 ? 0 : static_cast<std::uint64_t>(s.st_blocks);
    out.mode = linux_mode(s.st_mode);
    // Native app contexts report zero links, and Linux treats that as a deleted file.
    out.nlink = s.st_nlink ? static_cast<std::uint32_t>(s.st_nlink) : 1;
#if defined(__APPLE__)
    out.mtime_sec = static_cast<std::uint64_t>(s.st_mtimespec.tv_sec);
    out.mtime_nsec = static_cast<std::uint32_t>(s.st_mtimespec.tv_nsec);
#else
    out.mtime_sec = static_cast<std::uint64_t>(s.st_mtim.tv_sec);
    out.mtime_nsec = static_cast<std::uint32_t>(s.st_mtim.tv_nsec);
#endif
}
// Native app contexts can reject lstat for ordinary readable files, and stat never opens anything.
int metadata(const char *path, struct stat &s) noexcept
{
    if (::lstat(path, &s) == 0)
        return 0;
    return ::stat(path, &s) == 0 ? 0 : mapped_error(errno);
}
// Any "ps5term/ssh" or "ps5term/linux" component pair, however the path reaches it: on the whole
// console, /data also appears as /user/data. These hold the SSH password and host key, and the
// disk this guest is running from. A guest program with network access must not reach them.
bool protected_path(std::string_view path) noexcept
{
    std::string_view previous;
    while (!path.empty())
    {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (previous == "ps5term" && (part == "ssh" || part == "linux"))
            return true;
        previous = part;
        path = slash == path.npos ? std::string_view{} : path.substr(slash + 1);
    }
    return false;
}
// A "ps5term" directory can hold the protected pair; renaming or removing it would move the
// secrets to a name the pair check no longer recognises.
bool encloses_protected(std::string_view path) noexcept
{
    while (!path.empty() && path.back() == '/')
        path.remove_suffix(1);
    const auto slash = path.rfind('/');
    return (slash == path.npos ? path : path.substr(slash + 1)) == "ps5term";
}
} // namespace
bool LinuxSharedFolder::open(const char *path) noexcept
{
    close();
    error_ = 0;
    if (!path || !*path || std::strlen(path) >= root_.size())
    {
        error_ = EINVAL;
        return false;
    }
    int n = std::snprintf(root_.data(), root_.size(), "%s", path);
    while (n > 0 && root_[static_cast<std::size_t>(n) - 1] == '/')
        root_[static_cast<std::size_t>(--n)] = 0; // "/" becomes "", joined with "/" below.
    struct stat s
    {
    };
    if (::stat(path, &s) < 0)
    {
        error_ = errno;
        return false;
    }
    if (!S_ISDIR(s.st_mode))
    {
        error_ = ENOTDIR;
        return false;
    }
    opened_ = true;
    return true;
}
void LinuxSharedFolder::close() noexcept
{
    for (auto &f : files_)
    {
        if (f.fd >= 0)
            ::close(f.fd);
        f = File{};
    }
    for (auto &r : readers_)
    {
        delete r.cursor;
        r.cursor = nullptr;
        r.active = false;
    }
    root_[0] = 0;
    opened_ = false;
}
Ps5LinuxShare LinuxSharedFolder::device() noexcept
{
    return {this,  stat,  fstat,  open_file, close_file, read, write,      resize, chmod,
            times, mkdir, remove, rename,    readlink,   list, list_close, space};
}
bool LinuxSharedFolder::native_path(const char *path, char *out, std::size_t size) const noexcept
{
    if (!opened_ || !path || *path == '/' || protected_path(path))
        return false;
    const int n = *path ? std::snprintf(out, size, "%s/%s", root_.data(), path)
                        : std::snprintf(out, size, "%s/", root_.data());
    return n > 0 && static_cast<std::size_t>(n) < size;
}
LinuxSharedFolder::File *LinuxSharedFolder::file(unsigned handle) noexcept
{
    if (!handle || handle > files_.size() || files_[handle - 1].fd < 0)
        return nullptr;
    return &files_[handle - 1];
}
int LinuxSharedFolder::stat(void *ctx, const char *path, Ps5ShareStat *out) noexcept
{
    char native[PS5_SHARE_PATH + 512];
    if (!static_cast<LinuxSharedFolder *>(ctx)->native_path(path, native, sizeof(native)))
        return -2;
    struct stat s
    {
    };
    const int r = metadata(native, s);
    if (r)
        return r;
    fill(s, *out);
    return 0;
}
int LinuxSharedFolder::fstat(void *ctx, unsigned handle, Ps5ShareStat *out) noexcept
{
    auto *f = static_cast<LinuxSharedFolder *>(ctx)->file(handle);
    if (!f)
        return -9;
    struct stat s
    {
    };
    if (::fstat(f->fd, &s) < 0)
        return mapped_error(errno);
    fill(s, *out);
    return 0;
}
int LinuxSharedFolder::open_file(void *ctx, const char *path, int flags, unsigned mode) noexcept
{
    auto &self = *static_cast<LinuxSharedFolder *>(ctx);
    char native[PS5_SHARE_PATH + 512];
    if (!self.native_path(path, native, sizeof(native)))
        return (flags & PS5_SHARE_CREATE) ? -13 : -2;
    std::size_t slot = 0;
    while (slot < self.files_.size() && self.files_[slot].fd >= 0)
        slot++;
    if (slot == self.files_.size())
        return -23;
    // Refuse before opening: an open alone can have effects on a device.
    struct stat s
    {
    };
    if (metadata(native, s) == 0 && !S_ISREG(s.st_mode))
        return S_ISDIR(s.st_mode) ? -21 : -1;
    int native_flags =
        (flags & PS5_SHARE_WRITE) ? ((flags & PS5_SHARE_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (flags & PS5_SHARE_CREATE)
        native_flags |= O_CREAT;
    if (flags & PS5_SHARE_EXCLUSIVE)
        native_flags |= O_EXCL;
    if (flags & PS5_SHARE_TRUNCATE)
        native_flags |= O_TRUNC;
    const int fd = ::open(native, native_flags | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                          static_cast<mode_t>(mode & 0777));
    if (fd < 0)
        return mapped_error(errno);
    if (::fstat(fd, &s) < 0 || !S_ISREG(s.st_mode))
    {
        ::close(fd);
        return -1;
    }
    self.files_[slot] = {fd, (flags & (PS5_SHARE_CREATE | PS5_SHARE_TRUNCATE)) != 0};
    return static_cast<int>(slot + 1);
}
void LinuxSharedFolder::close_file(void *ctx, unsigned handle) noexcept
{
    auto *f = static_cast<LinuxSharedFolder *>(ctx)->file(handle);
    if (!f)
        return;
    // Close is when a program considers its file done, and a USB drive may be pulled afterwards.
    if (f->written)
    {
        int r;
        do
        {
            r = ::fsync(f->fd);
        } while (r < 0 && errno == EINTR);
    }
    ::close(f->fd);
    *f = File{};
}
int LinuxSharedFolder::read(void *ctx, unsigned handle, std::uint64_t offset, void *buffer,
                            std::size_t size) noexcept
{
    auto *f = static_cast<LinuxSharedFolder *>(ctx)->file(handle);
    if (!f)
        return -9;
    if (size > PS5_SHARE_IO || offset > INT64_MAX)
        return -22;
    ssize_t r;
    do
    {
        r = ::pread(f->fd, buffer, size, static_cast<off_t>(offset));
    } while (r < 0 && errno == EINTR);
    return r < 0 ? mapped_error(errno) : static_cast<int>(r);
}
int LinuxSharedFolder::write(void *ctx, unsigned handle, std::uint64_t offset, const void *buffer,
                             std::size_t size) noexcept
{
    auto *f = static_cast<LinuxSharedFolder *>(ctx)->file(handle);
    if (!f)
        return -9;
    if (size > PS5_SHARE_IO || offset > INT64_MAX)
        return -22;
    ssize_t r;
    do
    {
        r = ::pwrite(f->fd, buffer, size, static_cast<off_t>(offset));
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        return mapped_error(errno);
    f->written = true;
    return static_cast<int>(r);
}
int LinuxSharedFolder::resize(void *ctx, unsigned handle, const char *path,
                              std::uint64_t size) noexcept
{
    auto &self = *static_cast<LinuxSharedFolder *>(ctx);
    if (size > INT64_MAX)
        return -22;
    if (handle)
    {
        auto *f = self.file(handle);
        if (!f)
            return -9;
        if (::ftruncate(f->fd, static_cast<off_t>(size)) < 0)
            return mapped_error(errno);
        f->written = true;
        return 0;
    }
    const int temporary = open_file(ctx, path, PS5_SHARE_WRITE, 0);
    if (temporary < 0)
        return temporary;
    auto *f = self.file(static_cast<unsigned>(temporary));
    const int r = ::ftruncate(f->fd, static_cast<off_t>(size)) < 0 ? mapped_error(errno) : 0;
    f->written = true;
    close_file(ctx, static_cast<unsigned>(temporary));
    return r;
}
int LinuxSharedFolder::chmod(void *ctx, const char *path, unsigned mode) noexcept
{
    char native[PS5_SHARE_PATH + 512];
    if (!static_cast<LinuxSharedFolder *>(ctx)->native_path(path, native, sizeof(native)))
        return -2;
    // chmod follows a final symlink; never change a file outside the share through one.
    struct stat s
    {
    };
    if (::lstat(native, &s) == 0)
    {
        if (S_ISLNK(s.st_mode))
            return -1;
    }
    else
    {
        // Native app contexts can reject lstat; a no-follow open still tells links apart.
        const int fd = ::open(native, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            return errno == ELOOP || errno == EMLINK ? -1 : mapped_error(errno);
        ::close(fd);
    }
    return ::chmod(native, static_cast<mode_t>(mode & 07777)) < 0 ? mapped_error(errno) : 0;
}
int LinuxSharedFolder::times(void *ctx, const char *path, std::uint64_t a_sec, std::uint32_t a_nsec,
                             std::uint64_t m_sec, std::uint32_t m_nsec, int now) noexcept
{
    auto &self = *static_cast<LinuxSharedFolder *>(ctx);
    char native[PS5_SHARE_PATH + 512];
    if (!self.native_path(path, native, sizeof(native)))
        return -2;
    struct stat s
    {
    };
    const int known = metadata(native, s);
    if (known)
        return known;
    // Directory timestamps follow their contents; a device must not be opened for this.
    if (!S_ISREG(s.st_mode))
        return 0;
    const int fd = ::open(native, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return mapped_error(errno);
    struct timeval values[2]{};
    values[0].tv_sec = static_cast<time_t>(a_sec);
    values[0].tv_usec = static_cast<suseconds_t>(a_nsec / 1000);
    values[1].tv_sec = static_cast<time_t>(m_sec);
    values[1].tv_usec = static_cast<suseconds_t>(m_nsec / 1000);
    const int r = ::futimes(fd, now ? nullptr : values) < 0 ? mapped_error(errno) : 0;
    ::close(fd);
    return r;
}
int LinuxSharedFolder::mkdir(void *ctx, const char *path, unsigned mode) noexcept
{
    char native[PS5_SHARE_PATH + 512];
    if (!static_cast<LinuxSharedFolder *>(ctx)->native_path(path, native, sizeof(native)))
        return -13;
    return ::mkdir(native, static_cast<mode_t>(mode & 0777)) < 0 ? mapped_error(errno) : 0;
}
int LinuxSharedFolder::remove(void *ctx, const char *path) noexcept
{
    char native[PS5_SHARE_PATH + 512];
    if (!*path || !static_cast<LinuxSharedFolder *>(ctx)->native_path(path, native, sizeof(native)))
        return -2;
    if (encloses_protected(path))
        return -13;
    struct stat s
    {
    };
    const int known = metadata(native, s);
    if (known)
        return known;
    const int r = S_ISDIR(s.st_mode) ? ::rmdir(native) : ::unlink(native);
    return r < 0 ? mapped_error(errno) : 0;
}
int LinuxSharedFolder::rename(void *ctx, const char *from, const char *to) noexcept
{
    auto &self = *static_cast<LinuxSharedFolder *>(ctx);
    char source[PS5_SHARE_PATH + 512], target[PS5_SHARE_PATH + 512];
    if (!*from || !*to || !self.native_path(from, source, sizeof(source)) ||
        !self.native_path(to, target, sizeof(target)) || encloses_protected(from) ||
        encloses_protected(to))
        return -13;
    return ::rename(source, target) < 0 ? mapped_error(errno) : 0;
}
int LinuxSharedFolder::readlink(void *ctx, const char *path, char *out, std::size_t size) noexcept
{
    char native[PS5_SHARE_PATH + 512];
    if (!size || !static_cast<LinuxSharedFolder *>(ctx)->native_path(path, native, sizeof(native)))
        return -2;
#if defined(__PROSPERO__)
    (void)out;
    return -22; // Listed, but not followable: no app-safe readlink exists.
#else
    const auto n = ::readlink(native, out, size - 1);
    if (n < 0)
        return mapped_error(errno);
    out[n] = 0;
    return 0;
#endif
}
LinuxSharedFolder::Reader *LinuxSharedFolder::reader(unsigned key, const char *path) noexcept
{
    Reader *chosen = nullptr;
    for (auto &r : readers_)
    {
        if (r.active && r.key == key && !std::strcmp(r.path.data(), path))
            return &r;
        // Prefer a free reader, then the one idle longest. An evicted reader restarts by counting
        // entries when it next reads, which is correct unless entries were removed meanwhile.
        if (!chosen || (chosen->active && (!r.active || r.used < chosen->used)))
            chosen = &r;
    }
    if (!chosen)
        return nullptr;
    if (!chosen->cursor)
        chosen->cursor = new (std::nothrow) DirectoryCursor;
    if (!chosen->cursor)
        return nullptr;
    chosen->cursor->close();
    chosen->active = true;
    chosen->key = key;
    chosen->ended = chosen->has_name = false;
    chosen->consumed = 0;
    std::snprintf(chosen->path.data(), chosen->path.size(), "%s", path);
    char native[PS5_SHARE_PATH + 512];
    if (!native_path(path, native, sizeof(native)) || !chosen->cursor->open(native))
    {
        chosen->active = false;
        return nullptr;
    }
    return chosen;
}
int LinuxSharedFolder::list(void *ctx, unsigned key, const char *path, std::uint64_t cookie,
                            char *name, std::size_t size) noexcept
{
    auto &self = *static_cast<LinuxSharedFolder *>(ctx);
    if (size < PS5_SHARE_NAME)
        return -22;
    char checked[PS5_SHARE_PATH + 512];
    if (!self.native_path(path, checked, sizeof(checked)))
        return -2;
    errno = 0;
    auto *r = self.reader(key, path);
    if (!r)
        return errno ? mapped_error(errno) : -12;
    r->used = ++self.clock_;
    // The guest repeats an entry that did not fit in its last reply.
    if (r->has_name && r->consumed && cookie == r->consumed - 1)
    {
        std::snprintf(name, size, "%s", r->name.data());
        return 1;
    }
    if (cookie < r->consumed)
    {
        char native[PS5_SHARE_PATH + 512];
        r->cursor->close();
        r->consumed = 0;
        r->ended = r->has_name = false;
        if (!self.native_path(path, native, sizeof(native)) || !r->cursor->open(native))
        {
            r->active = false;
            return errno ? mapped_error(errno) : -5;
        }
    }
    if (r->ended)
        return 0;
    for (;;)
    {
        std::string_view entry;
        const auto step = r->cursor->next(entry);
        if (step == DirectoryStep::end)
        {
            r->ended = true;
            r->has_name = false;
            return 0;
        }
        if (step == DirectoryStep::failed)
            return mapped_error(errno);
        if (entry == "." || entry == ".." || entry.empty() || entry.size() >= PS5_SHARE_NAME)
            continue;
        char joined[PS5_SHARE_PATH];
        const int n = *path ? std::snprintf(joined, sizeof(joined), "%s/%.*s", path,
                                            static_cast<int>(entry.size()), entry.data())
                            : std::snprintf(joined, sizeof(joined), "%.*s",
                                            static_cast<int>(entry.size()), entry.data());
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(joined) || protected_path(joined))
            continue;
        if (r->consumed++ != cookie)
            continue;
        std::memcpy(r->name.data(), entry.data(), entry.size());
        r->name[entry.size()] = 0;
        r->has_name = true;
        std::snprintf(name, size, "%s", r->name.data());
        return 1;
    }
}
void LinuxSharedFolder::list_close(void *ctx, unsigned key) noexcept
{
    for (auto &r : static_cast<LinuxSharedFolder *>(ctx)->readers_)
        if (r.active && r.key == key)
        {
            r.cursor->close();
            r.active = false;
        }
}
int LinuxSharedFolder::space(void *ctx, Ps5ShareSpace *out) noexcept
{
    auto &self = *static_cast<LinuxSharedFolder *>(ctx);
    char native[PS5_SHARE_PATH + 512];
    if (!self.native_path("", native, sizeof(native)))
        return -5;
    struct statfs s
    {
    };
#if defined(__PROSPERO__)
    const int directory = ::open(native, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0)
        return mapped_error(errno);
    const int r = _fstatfs(directory, &s), saved = errno;
    ::close(directory);
    if (r < 0)
        return mapped_error(saved);
#else
    if (::statfs(native, &s) < 0)
        return mapped_error(errno);
#endif
    *out = {};
    out->block_size = static_cast<std::uint64_t>(s.f_bsize);
    out->blocks = static_cast<std::uint64_t>(s.f_blocks);
    out->free = static_cast<std::uint64_t>(s.f_bfree);
    out->available = s.f_bavail < 0 ? 0 : static_cast<std::uint64_t>(s.f_bavail);
    out->files = static_cast<std::uint64_t>(s.f_files);
    out->files_free = s.f_ffree < 0 ? 0 : static_cast<std::uint64_t>(s.f_ffree);
    return 0;
}
} // namespace term::platform
