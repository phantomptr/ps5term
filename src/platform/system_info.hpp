// PS5Term - process list and system facts from libkernel sysctls. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
namespace term::platform
{
// FreeBSD amd64 kinfo_proc field offsets (PS5 kernel). The native build checks
// these against the SDK's sys/user.h.
inline constexpr std::size_t kinfo_pid = 72, kinfo_ppid = 76, kinfo_uid = 168, kinfo_rssize = 264,
                             kinfo_runtime = 328, kinfo_stat = 388, kinfo_comm = 447,
                             kinfo_comm_size = 20, kinfo_min_size = kinfo_comm + kinfo_comm_size;
struct ProcessRecord
{
    int pid = 0, ppid = 0;
    std::uint32_t uid = 0;
    std::uint64_t rss_pages = 0, runtime_us = 0;
    char state = '?';
    char name[kinfo_comm_size]{};
};
struct ParseResult
{
    std::size_t count = 0;
    bool truncated = false; // more records than capacity
    bool malformed = false; // stopped at a record with an invalid size
};
// Parse a kern.proc.proc buffer. Never reads past `size`; stops on a bad record.
ParseResult parse_kinfo_proc(const unsigned char *data, std::size_t size, ProcessRecord *out,
                             std::size_t capacity) noexcept;
enum class LayoutCheck : std::uint8_t
{
    verified,   // the app's own record matches getpid/getppid/getuid
    mismatch,   // the app's record exists but other fields disagree
    self_absent // the app is not in the list
};
LayoutCheck check_layout(const ProcessRecord *records, std::size_t count, int self_pid,
                         int self_ppid, std::uint32_t self_uid) noexcept;
struct ProcessSnapshot
{
    ProcessRecord *records = nullptr; // caller-owned storage
    std::size_t capacity = 0;
    ParseResult parsed{};
    LayoutCheck layout = LayoutCheck::self_absent;
    bool self_only = false; // host fallback, or the kernel showed only the app
    int error = 0;          // errno from sysctl, 0 on success
};
// Fills snapshot.records. On hosts without kinfo_proc, reports only this process.
void snapshot_processes(ProcessSnapshot &snapshot) noexcept;

// Signal names accepted by kill: "TERM", "SIGTERM", "15" -> 15. Returns -1 if unknown.
int parse_signal(const char *text) noexcept;
const char *signal_name(int number) noexcept;
// Writes "HUP INT ..." for kill -l.
std::size_t signal_list(char *out, std::size_t capacity) noexcept;
enum class KillRefusal : std::uint8_t
{
    none,
    init,
    self
};
KillRefusal kill_refusal(long pid, int self_pid) noexcept;

struct SystemFacts
{
    char model[64] = "unknown", machine[32] = "unknown", ostype[32] = "unknown",
         osrelease[64] = "unknown", version[160] = "unknown";
    int cpus = 0;
    std::uint64_t physical_bytes = 0, free_bytes = 0, page_size = 0;
    bool free_known = false;
    std::uint32_t sdk_version = 0; // kern.sdk_version, 0 when absent
    long long boot_seconds = 0;    // kern.boottime, 0 when absent
    long long now_seconds = 0;
};
void collect_system_facts(SystemFacts &facts) noexcept;
// One read-only sysctl as text: a string, a 2/4/8-byte number, or bounded hex bytes.
// Returns 0 or an errno value.
int format_sysctl(const char *name, char *out, std::size_t capacity) noexcept;
// "13.60" from 0x13600000-style BCD words; "unknown" for 0.
void format_sdk_version(std::uint32_t value, char *out, std::size_t capacity) noexcept;
// "3 days, 4:05" style; "unknown" when either input is zero or negative.
void format_uptime(long long boot_seconds, long long now_seconds, char *out,
                   std::size_t capacity) noexcept;
// "1.5G", "512M", "40K", "17B"
void format_bytes(std::uint64_t value, char *out, std::size_t capacity) noexcept;
} // namespace term::platform
