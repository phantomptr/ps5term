// PS5Term - process list, signals and system facts. GPL-3.0-or-later.
#include "builtin_shell.hpp"
#include "platform/system_info.hpp"
#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
using namespace term::platform;
namespace
{
constexpr std::size_t record_size = 1088; // sizeof(kinfo_proc) on FreeBSD amd64
template <typename T> void put(std::vector<unsigned char> &b, std::size_t at, T v)
{
    std::memcpy(b.data() + at, &v, sizeof v);
}
void add(std::vector<unsigned char> &b, int pid, int ppid, std::uint32_t uid, unsigned char stat,
         const char *name, std::uint64_t rss = 0, std::uint64_t runtime = 0,
         std::size_t size = record_size)
{
    const std::size_t at = b.size();
    b.resize(at + size);
    put<int>(b, at, static_cast<int>(size));
    put<int>(b, at + kinfo_pid, pid);
    put<int>(b, at + kinfo_ppid, ppid);
    put<std::uint32_t>(b, at + kinfo_uid, uid);
    put<std::uint64_t>(b, at + kinfo_rssize, rss);
    put<std::uint64_t>(b, at + kinfo_runtime, runtime);
    b[at + kinfo_stat] = stat;
    std::memcpy(b.data() + at + kinfo_comm, name, std::min(std::strlen(name), kinfo_comm_size - 1));
}
std::string drain(term::BuiltinShell &shell, std::string *errors = nullptr)
{
    std::string out;
    char bytes[97];
    for (unsigned i = 0; i < 10000; ++i)
    {
        shell.tick();
        while (auto n = shell.read(bytes, sizeof bytes))
            out.append(bytes, n);
        if (errors)
            while (auto n = shell.read_error(bytes, sizeof bytes))
                errors->append(bytes, n);
        if (!shell.busy())
            return out;
    }
    assert(false);
    return out;
}
std::string run(term::BuiltinShell &shell, const std::string &command,
                std::string *errors = nullptr)
{
    assert(shell.submit(command));
    return drain(shell, errors);
}
} // namespace

int main()
{
    // Parser: fields, state letters, control characters in names.
    std::vector<unsigned char> buffer;
    add(buffer, 1, 0, 0, 3, "init");
    add(buffer, 42, 1, 1, 2, "eboot.bin", 2560, 61500000);
    add(buffer, 77, 1, 0, 5, "bad\x01name");
    ProcessRecord records[8];
    auto parsed = parse_kinfo_proc(buffer.data(), buffer.size(), records, 8);
    assert(parsed.count == 3 && !parsed.truncated && !parsed.malformed);
    assert(records[1].pid == 42 && records[1].ppid == 1 && records[1].uid == 1);
    assert(records[1].state == 'R' && records[1].rss_pages == 2560);
    assert(records[1].runtime_us == 61500000 && std::strcmp(records[1].name, "eboot.bin") == 0);
    assert(records[0].state == 'S' && records[2].state == 'Z');
    assert(std::strcmp(records[2].name, "bad?name") == 0);
    // Capacity, truncated tail, bad size and undersized records.
    parsed = parse_kinfo_proc(buffer.data(), buffer.size(), records, 2);
    assert(parsed.count == 2 && parsed.truncated);
    parsed = parse_kinfo_proc(buffer.data(), buffer.size() - 10, records, 8);
    assert(parsed.count == 2 && parsed.malformed);
    std::vector<unsigned char> tiny;
    add(tiny, 5, 1, 0, 2, "x", 0, 0, kinfo_min_size - 1);
    assert(parse_kinfo_proc(tiny.data(), tiny.size(), records, 8).malformed);
    std::vector<unsigned char> negative(record_size);
    put<int>(negative, 0, -1);
    assert(parse_kinfo_proc(negative.data(), negative.size(), records, 8).count == 0);
    assert(parse_kinfo_proc(nullptr, 100, records, 8).count == 0);
    assert(parse_kinfo_proc(buffer.data(), 3, records, 8).count == 0);
    // A longer record from a newer kernel still parses and advances by its own size.
    std::vector<unsigned char> longer;
    add(longer, 9, 1, 0, 2, "first", 0, 0, record_size + 64);
    add(longer, 10, 1, 0, 2, "second");
    parsed = parse_kinfo_proc(longer.data(), longer.size(), records, 8);
    assert(parsed.count == 2 && records[1].pid == 10);

    // Layout check against the app's own identity.
    parsed = parse_kinfo_proc(buffer.data(), buffer.size(), records, 8);
    assert(check_layout(records, parsed.count, 42, 1, 1) == LayoutCheck::verified);
    assert(check_layout(records, parsed.count, 42, 2, 1) == LayoutCheck::mismatch);
    assert(check_layout(records, parsed.count, 42, 1, 0) == LayoutCheck::mismatch);
    assert(check_layout(records, parsed.count, 43, 1, 1) == LayoutCheck::self_absent);

    // Signals.
    assert(parse_signal("TERM") == SIGTERM && parse_signal("SIGKILL") == SIGKILL);
    assert(parse_signal("9") == SIGKILL && parse_signal("0") == 0);
    assert(parse_signal("") == -1 && parse_signal("BOGUS") == -1 && parse_signal("99999") == -1);
    assert(parse_signal("12x") == -1 && parse_signal(nullptr) == -1);
    assert(std::strcmp(signal_name(SIGHUP), "HUP") == 0 && signal_name(999) == nullptr);
    char list[256];
    assert(signal_list(list, sizeof list) > 0 && std::strstr(list, "15) TERM"));
    assert(signal_list(list, 4) < 4);
    assert(kill_refusal(1, 50) == KillRefusal::init && kill_refusal(0, 50) == KillRefusal::init);
    assert(kill_refusal(-5, 50) == KillRefusal::init && kill_refusal(50, 50) == KillRefusal::self);
    assert(kill_refusal(51, 50) == KillRefusal::none);

    // Formatting.
    char text[64];
    format_sdk_version(0x13600000u, text, sizeof text);
    assert(std::strcmp(text, "13.60 (0x13600000)") == 0);
    format_sdk_version(0, text, sizeof text);
    assert(std::strcmp(text, "unknown") == 0);
    format_uptime(1000, 1000 + 3 * 86400 + 4 * 3600 + 5 * 60, text, sizeof text);
    assert(std::strcmp(text, "3 days, 4:05") == 0);
    format_uptime(1000, 1000 + 59 * 60, text, sizeof text);
    assert(std::strcmp(text, "0:59") == 0);
    format_uptime(0, 1000, text, sizeof text);
    assert(std::strcmp(text, "unknown") == 0);
    format_uptime(2000, 1000, text, sizeof text);
    assert(std::strcmp(text, "unknown") == 0);
    format_bytes(17, text, sizeof text);
    assert(std::strcmp(text, "17B") == 0);
    format_bytes(1536, text, sizeof text);
    assert(std::strcmp(text, "1.5K") == 0);
    format_bytes(16ull << 30, text, sizeof text);
    assert(std::strcmp(text, "16G") == 0);

    // Host snapshot: always includes this process with matching identity.
    ProcessSnapshot snapshot;
    snapshot.records = records;
    snapshot.capacity = 8;
    snapshot_processes(snapshot);
    assert(snapshot.error == 0 && snapshot.parsed.count >= 1);
    assert(snapshot.layout == LayoutCheck::verified);

    // Shell commands.
    term::BuiltinShell shell("/tmp", false);
    std::string errors;
    const std::string self = std::to_string(getpid());
    auto out = run(shell, "ps");
    assert(out.find("PID") == 2 && out.find(self) != std::string::npos);
    assert(shell.exit_status() == 0);
    out = run(shell, "ps -ef");
    assert(out.find("PPID") != std::string::npos && out.find(self) != std::string::npos);
    out = run(shell, "ps aux | grep -c PID");
    assert(out == "1\n");
    run(shell, "ps -z", &errors);
    assert(shell.exit_status() == 2 && errors.find("usage: ps") != std::string::npos);
    errors.clear();
    out = run(shell, "kill -l");
    assert(out.find("9) KILL") != std::string::npos && shell.exit_status() == 0);
    run(shell, "kill 1", &errors);
    assert(shell.exit_status() == 1 && errors.find("refusing") != std::string::npos);
    errors.clear();
    run(shell, "kill -9 " + self, &errors);
    assert(shell.exit_status() == 1 && errors.find("PS5Term itself") != std::string::npos);
    errors.clear();
    run(shell, "kill -0 " + std::to_string(getppid()));
    assert(shell.exit_status() == 0);
    run(shell, "kill -s TERM 2147483000", &errors);
    assert(shell.exit_status() == 1 && errors.find("kill: 2147483000:") != std::string::npos);
    errors.clear();
    run(shell, "kill abc", &errors);
    assert(shell.exit_status() == 1 && errors.find("not a process ID") != std::string::npos);
    errors.clear();
    run(shell, "kill -BOGUS 5", &errors);
    assert(shell.exit_status() == 2);
    errors.clear();
    run(shell, "kill", &errors);
    assert(shell.exit_status() == 2);
    errors.clear();
    out = run(shell, "uptime");
    assert(out.rfind("up ", 0) == 0);
    out = run(shell, "sysinfo");
    assert(out.find("Model:") == 0 && out.find("App:       pid " + self) != std::string::npos);
    assert(out.find("Processes:") != std::string::npos);
#ifdef __APPLE__
    out = run(shell, "nproc");
    assert(std::stoi(out) >= 1);
    out = run(shell, "free -h");
    assert(out.find("Mem:") != std::string::npos);
#endif
#ifdef __APPLE__
    out = run(shell, "sysctl hw.ncpu kern.ostype");
    assert(out.find("hw.ncpu: ") == 0 && std::stoi(out.substr(9)) >= 1);
    assert(out.find("kern.ostype: Darwin\n") != std::string::npos);
    out = run(shell, "sysctl -n kern.ostype");
    assert(out == "Darwin\n");
    out = run(shell, "sysctl kern.boottime");
    assert(out.find("kern.boottime: ") == 0 && out.find(' ', 16) != std::string::npos);
    errors.clear();
    run(shell, "sysctl no.such.name", &errors);
    assert(shell.exit_status() == 1 && errors.find("sysctl: no.such.name:") == 0);
#endif
    errors.clear();
    run(shell, "sysctl kern.hostname=x", &errors);
    assert(shell.exit_status() == 1 && errors.find("not supported") != std::string::npos);
    run(shell, "sysctl", &errors);
    assert(shell.exit_status() == 2);
    run(shell, "free -x", &errors);
    assert(shell.exit_status() == 2);
    std::puts("system info: ok");
    return 0;
}
