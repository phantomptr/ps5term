// PS5Term - process list and system facts from libkernel sysctls. GPL-3.0-or-later.
#include "system_info.hpp"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__PROSPERO__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#define PS5TERM_HAVE_SYSCTL 1
#endif
#if defined(__PROSPERO__) || defined(__FreeBSD__)
#include <sys/user.h>
#include <cstddef>
#define PS5TERM_HAVE_KINFO 1
static_assert(offsetof(struct kinfo_proc, ki_pid) == term::platform::kinfo_pid);
static_assert(offsetof(struct kinfo_proc, ki_ppid) == term::platform::kinfo_ppid);
static_assert(offsetof(struct kinfo_proc, ki_uid) == term::platform::kinfo_uid);
static_assert(offsetof(struct kinfo_proc, ki_rssize) == term::platform::kinfo_rssize);
static_assert(offsetof(struct kinfo_proc, ki_runtime) == term::platform::kinfo_runtime);
static_assert(offsetof(struct kinfo_proc, ki_stat) == term::platform::kinfo_stat);
static_assert(offsetof(struct kinfo_proc, ki_comm) == term::platform::kinfo_comm);
static_assert(sizeof(((struct kinfo_proc *)nullptr)->ki_comm) == term::platform::kinfo_comm_size);
#endif
namespace term::platform
{
namespace
{
template <typename T> T load(const unsigned char *p) noexcept
{
    T value{};
    std::memcpy(&value, p, sizeof value);
    return value;
}
char state_letter(unsigned char stat) noexcept
{
    // FreeBSD SIDL..SZOMB = 1..5, SWAIT 6, SLOCK 7.
    switch (stat)
    {
    case 1:
        return 'I';
    case 2:
        return 'R';
    case 3:
        return 'S';
    case 4:
        return 'T';
    case 5:
        return 'Z';
    case 6:
        return 'W';
    case 7:
        return 'L';
    default:
        return '?';
    }
}
struct SignalEntry
{
    int number;
    const char *name;
};
constexpr SignalEntry signals[] = {
    {SIGHUP, "HUP"},   {SIGINT, "INT"},   {SIGQUIT, "QUIT"}, {SIGABRT, "ABRT"}, {SIGKILL, "KILL"},
    {SIGUSR1, "USR1"}, {SIGUSR2, "USR2"}, {SIGPIPE, "PIPE"}, {SIGALRM, "ALRM"}, {SIGTERM, "TERM"},
    {SIGCONT, "CONT"}, {SIGSTOP, "STOP"}, {SIGTSTP, "TSTP"}};
void copy_text(char *out, std::size_t capacity, const char *text) noexcept
{
    if (capacity)
        std::snprintf(out, capacity, "%s", text);
}
#ifdef PS5TERM_HAVE_SYSCTL
void read_string(const char *name, char *out, std::size_t capacity) noexcept
{
    char buffer[256]{};
    std::size_t length = sizeof buffer - 1;
    if (sysctlbyname(name, buffer, &length, nullptr, 0) == 0 && length && buffer[0])
    {
        buffer[length < sizeof buffer ? length : sizeof buffer - 1] = 0;
        // Kernel version strings end in a newline; keep one line.
        for (char *c = buffer; *c; ++c)
            if (*c == '\n')
            {
                *c = 0;
                break;
            }
        // Some PS5 identity strings are space padded.
        for (std::size_t n = std::strlen(buffer);
             n && (buffer[n - 1] == ' ' || buffer[n - 1] == '\t');)
            buffer[--n] = 0;
        if (buffer[0])
            copy_text(out, capacity, buffer);
    }
}
// Integer sysctls are int, long or uint64 depending on the name; accept 4 or 8 bytes.
bool read_number(const char *name, std::uint64_t &value) noexcept
{
    unsigned char buffer[8]{};
    std::size_t length = sizeof buffer;
    if (sysctlbyname(name, buffer, &length, nullptr, 0) != 0)
        return false;
    if (length == 4)
        value = load<std::uint32_t>(buffer);
    else if (length == 8)
        value = load<std::uint64_t>(buffer);
    else
        return false;
    return true;
}
#endif
} // namespace

ParseResult parse_kinfo_proc(const unsigned char *data, std::size_t size, ProcessRecord *out,
                             std::size_t capacity) noexcept
{
    ParseResult result;
    if (!data)
        return result;
    std::size_t offset = 0;
    while (size - offset >= sizeof(int))
    {
        const int record_size = load<int>(data + offset);
        if (record_size < static_cast<int>(kinfo_min_size) ||
            static_cast<std::size_t>(record_size) > size - offset)
        {
            result.malformed = true;
            break;
        }
        if (result.count == capacity)
        {
            result.truncated = true;
            break;
        }
        const unsigned char *p = data + offset;
        ProcessRecord &r = out[result.count++];
        r.pid = load<int>(p + kinfo_pid);
        r.ppid = load<int>(p + kinfo_ppid);
        r.uid = load<std::uint32_t>(p + kinfo_uid);
        r.rss_pages = load<std::uint64_t>(p + kinfo_rssize);
        r.runtime_us = load<std::uint64_t>(p + kinfo_runtime);
        r.state = state_letter(p[kinfo_stat]);
        std::size_t n = 0;
        for (; n + 1 < sizeof r.name && p[kinfo_comm + n]; ++n)
        {
            const unsigned char c = p[kinfo_comm + n];
            r.name[n] = (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?';
        }
        r.name[n] = 0;
        offset += static_cast<std::size_t>(record_size);
    }
    return result;
}

LayoutCheck check_layout(const ProcessRecord *records, std::size_t count, int self_pid,
                         int self_ppid, std::uint32_t self_uid) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
        if (records[i].pid == self_pid)
            return records[i].ppid == self_ppid && records[i].uid == self_uid
                       ? LayoutCheck::verified
                       : LayoutCheck::mismatch;
    return LayoutCheck::self_absent;
}

void snapshot_processes(ProcessSnapshot &s) noexcept
{
    s.parsed = {};
    s.error = 0;
    s.self_only = false;
    const int self = static_cast<int>(getpid()), parent = static_cast<int>(getppid());
    const auto uid = static_cast<std::uint32_t>(getuid());
#ifdef PS5TERM_HAVE_KINFO
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    std::size_t needed = 0;
    if (sysctl(mib, 4, nullptr, &needed, nullptr, 0) != 0)
    {
        s.error = errno ? errno : EIO;
        return;
    }
    // Processes may start between the two calls; leave headroom, cap the total.
    constexpr std::size_t limit = 8u * 1024 * 1024;
    std::size_t room = needed + needed / 4 + 4096;
    if (room > limit)
        room = limit;
    auto *buffer = new (std::nothrow) unsigned char[room];
    if (!buffer)
    {
        s.error = ENOMEM;
        return;
    }
    std::size_t got = room;
    if (sysctl(mib, 4, buffer, &got, nullptr, 0) != 0)
    {
        s.error = errno ? errno : EIO;
        delete[] buffer;
        return;
    }
    s.parsed = parse_kinfo_proc(buffer, got, s.records, s.capacity);
    delete[] buffer;
    s.layout = check_layout(s.records, s.parsed.count, self, parent, uid);
    s.self_only = s.parsed.count == 1 && s.layout != LayoutCheck::self_absent;
#else
    if (!s.capacity)
        return;
    ProcessRecord &r = s.records[0];
    r = {};
    r.pid = self;
    r.ppid = parent;
    r.uid = uid;
    r.state = 'R';
    copy_text(r.name, sizeof r.name, "ps5term");
    s.parsed.count = 1;
    s.layout = LayoutCheck::verified;
    s.self_only = true;
#endif
}

int parse_signal(const char *text) noexcept
{
    if (!text || !*text)
        return -1;
    if (*text >= '0' && *text <= '9')
    {
        int value = 0;
        for (const char *c = text; *c; ++c)
        {
            if (*c < '0' || *c > '9' || value > 1000)
                return -1;
            value = value * 10 + (*c - '0');
        }
        if (value == 0)
            return 0;
        for (const auto &s : signals)
            if (s.number == value)
                return value;
        return -1;
    }
    if (std::strncmp(text, "SIG", 3) == 0)
        text += 3;
    for (const auto &s : signals)
        if (std::strcmp(text, s.name) == 0)
            return s.number;
    return -1;
}

const char *signal_name(int number) noexcept
{
    for (const auto &s : signals)
        if (s.number == number)
            return s.name;
    return nullptr;
}

std::size_t signal_list(char *out, std::size_t capacity) noexcept
{
    std::size_t used = 0;
    if (!capacity)
        return 0;
    out[0] = 0;
    for (const auto &s : signals)
    {
        const int n = std::snprintf(out + used, capacity - used, "%s%d) %s", used ? " " : "",
                                    s.number, s.name);
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - used)
            break;
        used += static_cast<std::size_t>(n);
    }
    return used;
}

KillRefusal kill_refusal(long pid, int self_pid) noexcept
{
    if (pid <= 1)
        return KillRefusal::init;
    if (pid == self_pid)
        return KillRefusal::self;
    return KillRefusal::none;
}

void collect_system_facts(SystemFacts &f) noexcept
{
    f = {};
#ifdef PS5TERM_HAVE_SYSCTL
    read_string("hw.model", f.model, sizeof f.model);
    read_string("hw.machine", f.machine, sizeof f.machine);
    read_string("kern.ostype", f.ostype, sizeof f.ostype);
    read_string("kern.osrelease", f.osrelease, sizeof f.osrelease);
    read_string("kern.version", f.version, sizeof f.version);
    std::uint64_t value = 0;
    if (read_number("hw.ncpu", value))
        f.cpus = static_cast<int>(value);
    if (read_number("hw.pagesize", value) || read_number("vm.stats.vm.v_page_size", value))
        f.page_size = value;
    // Apps may be refused some of these; take the first the kernel answers.
    if (read_number("hw.memsize", value) || read_number("hw.physmem", value) ||
        read_number("hw.realmem", value) || read_number("hw.usermem", value))
        f.physical_bytes = value;
    else if (f.page_size && read_number("vm.stats.vm.v_page_count", value))
        f.physical_bytes = value * f.page_size;
    if (f.page_size && read_number("vm.stats.vm.v_free_count", value))
    {
        f.free_bytes = value * f.page_size;
        f.free_known = true;
    }
    if (read_number("kern.sdk_version", value))
        f.sdk_version = static_cast<std::uint32_t>(value);
    struct timeval boot
    {
    };
    std::size_t length = sizeof boot;
    if (sysctlbyname("kern.boottime", &boot, &length, nullptr, 0) == 0 && length == sizeof boot)
        f.boot_seconds = static_cast<long long>(boot.tv_sec);
#endif
    struct timespec now
    {
    };
    if (clock_gettime(CLOCK_REALTIME, &now) == 0)
        f.now_seconds = static_cast<long long>(now.tv_sec);
}

int format_sysctl(const char *name, char *out, std::size_t capacity) noexcept
{
    if (!name || !*name || !out || capacity < 2)
        return EINVAL;
    out[0] = 0;
#ifdef PS5TERM_HAVE_SYSCTL
    unsigned char buffer[1024]{};
    std::size_t length = sizeof buffer;
    if (sysctlbyname(name, buffer, &length, nullptr, 0) != 0)
        return errno ? errno : EIO;
    if (length > sizeof buffer)
        length = sizeof buffer;
    // Text: printable bytes, optionally NUL terminated.
    std::size_t text = length;
    while (text && buffer[text - 1] == 0)
        --text;
    bool printable = text > 0;
    for (std::size_t i = 0; i < text && printable; ++i)
        printable =
            (buffer[i] >= 0x20 && buffer[i] < 0x7f) || buffer[i] == '\n' || buffer[i] == '\t';
    // A 2/4/8-byte value is a number unless it is exactly a NUL-terminated string.
    const bool number_sized = length == 2 || length == 4 || length == 8;
    if (printable && (!number_sized || text + 1 == length))
    {
        while (text && (buffer[text - 1] == '\n' || buffer[text - 1] == ' '))
            --text;
        std::snprintf(out, capacity, "%.*s", static_cast<int>(text),
                      reinterpret_cast<const char *>(buffer));
        return 0;
    }
    if (length == 2)
        std::snprintf(out, capacity, "%u", static_cast<unsigned>(load<std::uint16_t>(buffer)));
    else if (length == 4)
        std::snprintf(out, capacity, "%u", load<std::uint32_t>(buffer));
    else if (length == 8)
        std::snprintf(out, capacity, "%llu",
                      static_cast<unsigned long long>(load<std::uint64_t>(buffer)));
    else
    {
        // Structures: show the bytes, bounded.
        std::size_t used = 0;
        for (std::size_t i = 0; i < length && i < 64 && used + 4 < capacity; ++i)
            used += static_cast<std::size_t>(
                std::snprintf(out + used, capacity - used, i ? " %02x" : "%02x", buffer[i]));
        if (length > 64 && used + 5 < capacity)
            std::snprintf(out + used, capacity - used, " ...");
    }
    return 0;
#else
    (void)name;
    return ENOTSUP;
#endif
}

void format_sdk_version(std::uint32_t value, char *out, std::size_t capacity) noexcept
{
    if (!value)
    {
        copy_text(out, capacity, "unknown");
        return;
    }
    std::snprintf(out, capacity, "%x.%02x (0x%08x)", (value >> 24) & 0xffu, (value >> 16) & 0xffu,
                  value);
}

void format_uptime(long long boot, long long now, char *out, std::size_t capacity) noexcept
{
    if (boot <= 0 || now <= 0 || now < boot)
    {
        copy_text(out, capacity, "unknown");
        return;
    }
    long long seconds = now - boot;
    const long long days = seconds / 86400;
    seconds %= 86400;
    const int hours = static_cast<int>(seconds / 3600),
              minutes = static_cast<int>((seconds % 3600) / 60);
    if (days)
        std::snprintf(out, capacity, "%lld day%s, %d:%02d", days, days == 1 ? "" : "s", hours,
                      minutes);
    else
        std::snprintf(out, capacity, "%d:%02d", hours, minutes);
}

void format_bytes(std::uint64_t value, char *out, std::size_t capacity) noexcept
{
    static constexpr const char units[] = "BKMGTP";
    double scaled = static_cast<double>(value);
    std::size_t unit = 0;
    while (scaled >= 1024.0 && unit + 1 < sizeof units - 1)
    {
        scaled /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        std::snprintf(out, capacity, "%lluB", static_cast<unsigned long long>(value));
    else if (scaled < 10.0)
        std::snprintf(out, capacity, "%.1f%c", scaled, units[unit]);
    else
        std::snprintf(out, capacity, "%.0f%c", scaled, units[unit]);
}
} // namespace term::platform
