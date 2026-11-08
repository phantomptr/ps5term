// PS5Term - GPL-3.0-or-later.
#include "linux_subsystem.hpp"
#include "../i18n.hpp"
#if defined(PS5TERM_LINUX_SUBSYSTEM)
#include "linux_session.h"
#if defined(PS5TERM_LINUX_NETWORK)
#include "../../backend/subsystem/net/nat.h"
#include <arpa/inet.h>
#include <sys/types.h>
#endif
#include "../../backend/subsystem/guest_memory.h"
#include <cstdio>
#include <new>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
namespace term::platform
{
namespace
{
// Kernel-backed bytes only, or nothing. A missing seed just delays guest entropy.
bool read_boot_seed(std::array<uint8_t, 64> &seed) noexcept
{
    const int fd = ::open("/dev/random", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return false;
    std::size_t total = 0;
    while (total < seed.size())
    {
        const auto n = ::read(fd, seed.data() + total, seed.size() - total);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        total += static_cast<std::size_t>(n);
    }
    ::close(fd);
    return total == seed.size();
}
uint8_t *read_file(const char *path, std::size_t &size, std::size_t limit, int &error) noexcept
{
    ps5_guest_trace("image-open", limit);
    int fd = ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
    {
        error = errno;
        return nullptr;
    }
    struct stat st
    {
    };
    if (::fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        static_cast<std::uint64_t>(st.st_size) > limit)
    {
        error = errno ? errno : EINVAL;
        ::close(fd);
        return nullptr;
    }
    auto *buf =
        static_cast<uint8_t *>(ps5_guest_buffer_alloc(static_cast<std::size_t>(st.st_size)));
    if (!buf)
    {
        error = ENOMEM;
        ::close(fd);
        return nullptr;
    }
    std::size_t total = 0;
    auto target = static_cast<std::size_t>(st.st_size);
    while (total < target)
    {
        auto n = ::read(fd, buf + total, target - total);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            error = n < 0 ? errno : EIO;
            ps5_guest_buffer_free(buf, target);
            ::close(fd);
            return nullptr;
        }
        total += static_cast<std::size_t>(n);
    }
    ::close(fd);
    ps5_guest_trace("image-loaded", target);
    size = target;
    return buf;
}
#if defined(PS5TERM_LINUX_NETWORK)
// Strict dotted IPv4: four decimal octets, optional trailing whitespace. Unspecified,
// loopback and multicast/reserved first octets are refused as upstream resolvers.
bool parse_resolver(const char *text, uint32_t &value) noexcept
{
    uint32_t result = 0;
    for (unsigned octet = 0; octet < 4; ++octet)
    {
        if (*text < '0' || *text > '9')
            return false;
        unsigned number = 0, digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 4)
        {
            number = number * 10 + static_cast<unsigned>(*text++ - '0');
            ++digits;
        }
        if (digits > 3 || number > 255)
            return false;
        if (octet < 3 && *text++ != '.')
            return false;
        result = (result << 8) | number;
    }
    while (*text == '\n' || *text == '\r' || *text == ' ' || *text == '\t')
        ++text;
    const unsigned first = result >> 24;
    if (*text || first == 0 || first == 127 || first >= 224)
        return false;
    value = result;
    return true;
}
// Optional override in <data directory>/resolver; the default is 1.1.1.1.
// Any unreadable, linked, oversized or malformed file selects the default.
uint32_t read_resolver(const char *data_directory, std::array<char, 16> &text) noexcept
{
    uint32_t value = 0x01010101u;
    char path[256];
    const int n = std::snprintf(path, sizeof path, "%s/resolver", data_directory);
    if (n > 0 && static_cast<std::size_t>(n) < sizeof path)
    {
        const int fd = ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0)
        {
            struct stat st
            {
            };
            char buffer[65];
            ssize_t got = -1;
            if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= 64)
                got = ::read(fd, buffer, sizeof buffer - 1);
            ::close(fd);
            uint32_t parsed = 0;
            if (got > 0)
            {
                buffer[got] = 0;
                if (parse_resolver(buffer, parsed))
                    value = parsed;
            }
        }
    }
    std::snprintf(text.data(), text.size(), "%u.%u.%u.%u", value >> 24, (value >> 16) & 255,
                  (value >> 8) & 255, value & 255);
    return htonl(value);
}
#endif
} // namespace

LinuxSubsystem::~LinuxSubsystem()
{
    stop();
}

bool LinuxSubsystem::load_images() noexcept
{
    char path[256];
    status_[0] = 0;
    const auto load = [&](const char *name, std::size_t &size, std::size_t limit)
    {
        const int n = std::snprintf(path, sizeof path, "%s/%s", directory_, name);
        int error = ENAMETOOLONG;
        auto *data = n > 0 && static_cast<std::size_t>(n) < sizeof path
                         ? read_file(path, size, limit, error)
                         : nullptr;
        if (!data)
            i18n::format(status_.data(), status_.size(), trc("Cannot read %s (errno %d)"), name,
                         error);
        return data;
    };
    bios_ = load("bbl64.bin", bios_size_, 1024 * 1024);
    if (bios_)
        kernel_ = load("kernel-riscv64.bin", kernel_size_, 32 * 1024 * 1024);
    if (kernel_)
        disk_ = load("root-riscv64.bin", disk_size_, 32 * 1024 * 1024);
    const auto le64 = [](const uint8_t *p)
    {
        std::uint64_t n = 0;
        for (unsigned i = 0; i < 8; ++i)
            n |= static_cast<std::uint64_t>(p[i]) << (8 * i);
        return n;
    };
    // Fixed raw firmware / little-endian RISC-V Image / sector-aligned ext2.
    // Exact image hashes are pinned by packaging; these checks reject malformed files.
    if (!bios_ || !kernel_ || !disk_ || bios_size_ < 4 || std::memcmp(bios_, "\177ELF", 4) == 0 ||
        kernel_size_ < 64 || std::memcmp(kernel_ + 56, "RSC\005", 4) ||
        le64(kernel_ + 8) != 0x200000 || !le64(kernel_ + 16) ||
        le64(kernel_ + 16) > 32 * 1024 * 1024 || (le64(kernel_ + 24) & 1) || disk_size_ < 1082 ||
        disk_size_ % 512 || disk_[1080] != 0x53 || disk_[1081] != 0xef)
    {
        if (!status_[0])
            i18n::format(status_.data(), status_.size(), trc("Unsupported Linux image format"));
        free_images();
        return false;
    }
    return true;
}

void LinuxSubsystem::free_images() noexcept
{
#if defined(PS5TERM_LINUX_NETWORK)
    ps5_linux_nat_destroy(network_);
    network_ = nullptr;
#endif
    ps5_guest_buffer_free(bios_, bios_size_);
    bios_ = nullptr;
    bios_size_ = 0;
    ps5_guest_buffer_free(kernel_, kernel_size_);
    kernel_ = nullptr;
    kernel_size_ = 0;
    ps5_guest_buffer_free(disk_, disk_size_);
    disk_ = nullptr;
    disk_size_ = 0;
}

#if defined(PS5TERM_LINUX_PERSISTENCE)
bool LinuxSubsystem::open_data_disk() noexcept
{
    if (!std::strcmp(data_directory_, "/data/ps5term/linux") &&
        ::mkdir("/data/ps5term", 0700) < 0 && errno != EEXIST)
    {
        i18n::format(status_.data(), status_.size(),
                     trc("Cannot create PS5Term data directory (errno %d)"), errno);
        return false;
    }
    if (::mkdir(data_directory_, 0700) < 0 && errno != EEXIST)
    {
        i18n::format(status_.data(), status_.size(),
                     trc("Cannot create Linux data directory (errno %d)"), errno);
        return false;
    }
    int parent = ::open(data_directory_, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    char path[256];
    const int length = std::snprintf(path, sizeof(path), "%s/data-seed.ext2", directory_);
    int seed = length > 0 && static_cast<std::size_t>(length) < sizeof(path)
                   ? ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)
                   : -1;
    if (parent < 0 || seed < 0)
    {
        const int error = errno;
        if (parent >= 0)
            ::close(parent);
        if (seed >= 0)
            ::close(seed);
        i18n::format(status_.data(), status_.size(),
                     trc("Cannot open Linux data directory/seed (errno %d)"), error);
        return false;
    }
    // A separately pinned 16 MiB ext2 seed. Existing disk is never reseeded.
    data_disk_ = new (std::nothrow) LinuxDataDisk;
    bool ok = data_disk_ && data_disk_->open(parent, seed, 16 * 1024 * 1024, data_directory_);
    ::close(seed);
    ::close(parent);
    if (!ok)
        i18n::format(status_.data(), status_.size(),
                     trc("Data disk: %s (errno %d); existing data preserved"),
                     data_disk_ ? data_disk_->operation() : "allocate",
                     data_disk_ ? data_disk_->error() : ENOMEM);
    return ok;
}
#endif

bool LinuxSubsystem::mounts_whole_console() const noexcept
{
    char path[512], value[8]{};
    const int n = std::snprintf(path, sizeof(path), "%s/host-mount", data_directory_);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(path))
        return false;
    const int file = ::open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0)
        return false;
    const auto got = ::read(file, value, sizeof(value) - 1);
    ::close(file);
    // Anything unexpected means the default: never widen access on a damaged file.
    return got >= 1 && value[0] == '/' && (got == 1 || value[1] == '\n');
}
bool LinuxSubsystem::set_mounts_whole_console(bool whole) noexcept
{
    char path[512], temporary[520];
    const int n = std::snprintf(path, sizeof(path), "%s/host-mount", data_directory_);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(path))
        return false;
    std::snprintf(temporary, sizeof(temporary), "%s.new", path);
    // The parents exist on any console where Linux has started; create them for one where it has
    // not. Only the two levels PS5Term owns, and a failure surfaces at the open below.
    char parent[512];
    std::snprintf(parent, sizeof(parent), "%s", data_directory_);
    if (char *slash = std::strrchr(parent, '/'); slash && slash != parent)
    {
        *slash = 0;
        ::mkdir(parent, 0700);
    }
    ::mkdir(data_directory_, 0700);
    const int file = ::open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (file < 0)
        return false;
    const char *value = whole ? "/\n" : "/data\n";
    const auto length = static_cast<ssize_t>(std::strlen(value));
    bool ok = ::write(file, value, static_cast<std::size_t>(length)) == length;
    ok = ::fsync(file) == 0 && ok;
    ok = ::close(file) == 0 && ok;
    if (!ok || ::rename(temporary, path) < 0)
    {
        ::unlink(temporary);
        return false;
    }
    return true;
}
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
// Linux is useful without PS5 files, so a failure here is reported rather than fatal.
void LinuxSubsystem::open_shared_folder() noexcept
{
    shared_error_ = 0;
    shared_folder_ = new (std::nothrow) LinuxSharedFolder;
    if (!shared_folder_)
    {
        shared_error_ = ENOMEM;
        return;
    }
    if (!shared_folder_->open(mounts_whole_console() ? console_root_ : data_root_))
    {
        shared_error_ = shared_folder_->error();
        delete shared_folder_;
        shared_folder_ = nullptr;
    }
}
#endif

bool LinuxSubsystem::start(unsigned cols, unsigned rows) noexcept
{
    if (running_ || storage_busy())
        return false;
    if (!stop())
        return false; // Join before replacing a failed worker or its images.
    if (!load_images())
        return false;
#if defined(PS5TERM_LINUX_PERSISTENCE)
    if (!open_data_disk())
    {
        delete data_disk_;
        data_disk_ = nullptr;
        free_images();
        return false;
    }
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
    open_shared_folder();
#endif
    ps5_guest_trace("images-valid", 0);
    session_ = ps5_linux_session_create(160 * 1024 * 1024);
    if (!session_)
    {
        i18n::format(status_.data(), status_.size(), trc("Cannot allocate Linux session"));
#if defined(PS5TERM_LINUX_PERSISTENCE)
        delete data_disk_;
        data_disk_ = nullptr;
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
        delete shared_folder_;
        shared_folder_ = nullptr;
#endif
        free_images();
        return false;
    }
    ps5_guest_trace("session-created", 0);
    ps5_linux_session_resize(session_, cols, rows);
    Ps5LinuxImages images{};
    images.bios = bios_;
    images.bios_size = bios_size_;
    images.kernel = kernel_;
    images.kernel_size = kernel_size_;
    images.disk = disk_;
    images.disk_size = disk_size_;
#if defined(PS5TERM_LINUX_PERSISTENCE)
    images.data_disk = data_disk_->device();
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
    if (shared_folder_)
        images.shared_folder = shared_folder_->device();
#endif
#if defined(PS5TERM_LINUX_NETWORK)
    Ps5LinuxNatConfig config{};
    config.dns_ipv4 = read_resolver(data_directory_, resolver_text_);
    config.dns_port = 53;
    config.fail_after = SIZE_MAX; // Guest cannot reach this app's own loopback services.
    network_ = ps5_linux_nat_create(&config);
    // A network setup failure leaves a usable offline Linux environment.
    if (network_)
        images.network = ps5_linux_nat_device(network_);
#endif
    if (read_boot_seed(rng_seed_))
    {
        images.rng_seed = rng_seed_.data();
        images.rng_seed_size = rng_seed_.size();
    }
    images.wall_clock = static_cast<std::int64_t>(std::time(nullptr));
    images.agent = 1;
    ps5_guest_trace("worker-request", 0);
    if (!ps5_linux_session_start(session_, &images))
    {
        const auto error = ps5_linux_session_status(session_);
        i18n::format(status_.data(), status_.size(),
                     trc("Linux worker start failed (error %d, thread %d)"),
                     static_cast<int>(error.error), error.thread_error);
        ps5_linux_session_destroy(session_);
        session_ = nullptr;
#if defined(PS5TERM_LINUX_PERSISTENCE)
        delete data_disk_;
        data_disk_ = nullptr;
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
        delete shared_folder_;
        shared_folder_ = nullptr;
#endif
        free_images();
        return false;
    }
    agent_.attach({session_,
                   [](void *session, const void *bytes, std::size_t size) noexcept {
                       return ps5_linux_session_agent_write(static_cast<Ps5LinuxSession *>(session),
                                                            bytes, size);
                   },
                   [](void *session, void *bytes, std::size_t size) noexcept {
                       return ps5_linux_session_agent_read(static_cast<Ps5LinuxSession *>(session),
                                                           bytes, size);
                   }});
    i18n::format(status_.data(), status_.size(), trc("Linux worker running"));
    ps5_guest_trace("worker-started", 0);
    running_ = true;
    return true;
}

bool LinuxSubsystem::stop() noexcept
{
    agent_.detach();
    if (session_ && !ps5_linux_session_stop(session_))
    {
        i18n::format(status_.data(), status_.size(),
                     trc("Cannot join Linux worker; session retained"));
        return false; // Never free borrowed images while a worker may still use them.
    }
    if (session_)
        ps5_linux_session_destroy(session_);
    session_ = nullptr;
#if defined(PS5TERM_LINUX_PERSISTENCE)
    delete data_disk_;
    data_disk_ = nullptr;
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
    delete shared_folder_;
    shared_folder_ = nullptr;
#endif
    const bool had_session = running_;
    running_ = false;
    free_images();
    if (had_session)
        i18n::format(status_.data(), status_.size(), trc("Linux stopped"));
    return true;
}

std::size_t LinuxSubsystem::read(char *buf, std::size_t size) noexcept
{
    if (!session_)
        return 0;
#if defined(PS5TERM_LINUX_PERSISTENCE)
    if (data_disk_ && data_disk_->error())
        i18n::format(status_.data(), status_.size(), trc("Linux disk I/O failed (errno %d)"),
                     data_disk_->error());
#endif
#if defined(PS5TERM_LINUX_NETWORK)
    auto network_status = ps5_linux_session_status(session_);
    if (network_status.state == PS5_SESSION_RUNNING)
    {
        if (network_status.network_state < 0)
            i18n::format(status_.data(), status_.size(),
                         trc("Linux running; network failed (socket layer). Reconnect or restart "
                             "Linux to retry."));
        else if (!network_)
            i18n::format(status_.data(), status_.size(),
                         trc("Linux running offline; network setup unavailable."));
        else if (network_status.network_state > 0)
            std::snprintf(
                status_.data(), status_.size(),
                "Linux running; network on. eth0 10.0.2.15 by DHCP, DNS via %s, no inbound.",
                resolver_text_.data());
    }
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
    if (shared_error_)
        i18n::format(status_.data(), status_.size(),
                     trc("Linux running; PS5 files could not be mounted at /mnt/ps5 (errno %d)."),
                     shared_error_);
#endif
    auto n = ps5_linux_session_read(session_, buf, size);
    if (!n)
    {
        auto status = ps5_linux_session_status(session_);
        if (status.state == PS5_SESSION_STOPPED || status.state == PS5_SESSION_FAILED)
            running_ = false;
    }
    return n;
}

std::size_t LinuxSubsystem::write(const char *buf, std::size_t size) noexcept
{
    if (!session_)
        return 0;
    return ps5_linux_session_write(session_, buf, size);
}

bool LinuxSubsystem::resize(unsigned cols, unsigned rows) noexcept
{
    if (!session_)
        return false;
    return ps5_linux_session_resize(session_, cols, rows);
}
} // namespace term::platform
#else
// Stubs when Linux subsystem is not compiled in.
namespace term::platform
{
LinuxSubsystem::~LinuxSubsystem()
{
}
bool LinuxSubsystem::start(unsigned, unsigned) noexcept
{
    return false;
}
bool LinuxSubsystem::stop() noexcept
{
    return true;
}
std::size_t LinuxSubsystem::read(char *, std::size_t) noexcept
{
    return 0;
}
std::size_t LinuxSubsystem::write(const char *, std::size_t) noexcept
{
    return 0;
}
bool LinuxSubsystem::resize(unsigned, unsigned) noexcept
{
    return false;
}
} // namespace term::platform
#endif
