// PS5Term GPL-3.0-or-later. Real guest: SSH-style sessions over the second console.
#include "linux_session.h"
#include "platform/guest_agent.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
using term::platform::GuestAgent;
using Clock = std::chrono::steady_clock;
static Ps5LinuxSession *session;
static GuestAgent agent;
static std::vector<unsigned char> load(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), {}};
}
static void drain_console()
{
    char bytes[4096];
    while (const auto count = ps5_linux_session_read(session, bytes, sizeof bytes))
        if (std::getenv("PS5TERM_BRIDGE_VERBOSE"))
            std::fwrite(bytes, 1, count, stderr);
}
template <class Done> static void pump_until(Done done, int seconds = 40, const char *step = "")
{
    const auto end = Clock::now() + std::chrono::seconds(seconds);
    while (!done())
    {
        if (Clock::now() >= end)
        {
            std::fprintf(stderr, "guest did not answer in time: %s\n", step);
            std::abort();
        }
        agent.pump();
        drain_console();
        assert(!agent.failed());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
static void until_output(int handle, std::string &seen, const std::string &marker)
{
    pump_until(
        [&]
        {
            char bytes[1024];
            while (const auto count = agent.read(handle, bytes, sizeof bytes))
                seen.append(bytes, count);
            return seen.find(marker) != seen.npos;
        },
        40, marker.c_str());
    if (std::getenv("PS5TERM_BRIDGE_VERBOSE"))
        std::fprintf(stderr, "[step] %s\n", marker.c_str());
}
static void send(int handle, std::string text)
{
    while (!text.empty())
    {
        const auto count = agent.write(handle, text);
        text.erase(0, count);
        agent.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    const std::string base = argv[1];
    const auto bios = load(base + "/bbl64.bin"), kernel = load(base + "/kernel-riscv64.bin"),
               disk = load(base + "/root-riscv64.bin");
    Ps5LinuxImages images{};
    images.bios = bios.data();
    images.bios_size = bios.size();
    images.kernel = kernel.data();
    images.kernel_size = kernel.size();
    images.disk = disk.data();
    images.disk_size = disk.size();
    images.wall_clock = 1793800000;
    images.agent = 1;
    session = ps5_linux_session_create(160 * 1024 * 1024);
    assert(session && ps5_linux_session_start(session, &images));
    agent.attach(
        {session, [](void *s, const void *b, std::size_t n) noexcept
         { return ps5_linux_session_agent_write(static_cast<Ps5LinuxSession *>(s), b, n); },
         [](void *s, void *b, std::size_t n) noexcept
         { return ps5_linux_session_agent_read(static_cast<Ps5LinuxSession *>(s), b, n); }});
    if (const char *probe = std::getenv("PS5TERM_BRIDGE_PROBE"))
    {
        std::this_thread::sleep_for(std::chrono::seconds(8));
        ps5_linux_session_write(session, probe, std::strlen(probe));
    }
    pump_until([] { return agent.alive(); }, 60);
    // Ctrl+O is nano's save key. The console driver must not take it for SysRq.
    {
        const auto console_until = [](const char *marker, int seconds)
        {
            std::string seen;
            const auto end = Clock::now() + std::chrono::seconds(seconds);
            while (seen.find(marker) == seen.npos)
            {
                if (Clock::now() >= end)
                {
                    std::fprintf(stderr, "console never showed %s:\n%s\n", marker, seen.c_str());
                    std::abort();
                }
                agent.pump();
                char bytes[1024];
                while (const auto count = ps5_linux_session_read(session, bytes, sizeof bytes))
                    seen.append(bytes, count);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        console_until(" # ", 60);
        const char *command =
            "stty raw -echo; head -c 3 | od -An -tx1 | tr -d ' ' | sed 's/^/KEYS_/'; stty sane\n";
        ps5_linux_session_write(session, command, std::strlen(command));
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        ps5_linux_session_write(session, "A\x0fz", 3);
        console_until("KEYS_410f7a", 20);
    }
    // Two independent shells with their own sizes and working directories.
    const int first = agent.open(132, 43, "xterm-ghostty");
    const int second = agent.open(80, 24, "screen-256color");
    assert(first > 0 && second > 0 && first != second);
    pump_until(
        [&]
        {
            return agent.state(first) == GuestAgent::State::running &&
                   agent.state(second) == GuestAgent::State::running;
        });
    std::string a, b;
    send(first, "cd /tmp; stty size; echo TERM=$TERM; test -t 0 && printf 'ONE_%s\\n' TTY\n");
    until_output(first, a, "ONE_TTY");
    assert(a.find("43 132") != a.npos && a.find("TERM=xterm-256color") != a.npos);
    send(second, "pwd; stty size; printf 'TWO_%s\\n' DONE\n");
    until_output(second, b, "TWO_DONE");
    assert(b.find("/root") != b.npos && b.find("24 80") != b.npos && b.find("/tmp") == b.npos);
    // Live resize reaches only that session.
    assert(agent.resize(second, 100, 30));
    b.clear();
    send(second, "stty size; printf 'TWO_%s\\n' AGAIN\n");
    until_output(second, b, "TWO_AGAIN");
    assert(b.find("30 100") != b.npos);
    // Ctrl+C stops a foreground job through the PTY's own line discipline.
    a.clear();
    send(first, "sleep 60\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    send(first, "\x03");
    send(first, "printf 'ONE_%s\\n' STOPPED\n");
    until_output(first, a, "ONE_STOPPED");
    // Large output flows through the credit window without loss.
    a.clear();
    send(first, "seq 1 20000 | tail -n +1; printf 'ONE_%s\\n' FLOOD\n");
    until_output(first, a, "ONE_FLOOD");
    assert(a.find("\r\n20000\r\n") != a.npos && a.find("\r\n10000\r\n") != a.npos);
    // A shell's exit status arrives after its output.
    send(first, "exit 9\n");
    pump_until(
        [&]
        {
            char ignored[1024];
            while (agent.read(first, ignored, sizeof ignored))
            {
            }
            return agent.state(first) == GuestAgent::State::exited;
        });
    int status = -1;
    agent.state(first, &status);
    assert(status == 9);
    agent.close(first);
    // Exec: exact bytes, end of input and status, the shape scp-like tools need.
    const int exec = agent.open(80, 24, "dumb", "cat; printf done; exit 4");
    assert(exec > 0);
    pump_until([&] { return agent.state(exec) == GuestAgent::State::running; });
    std::string payload;
    for (int i = 0; i < 2000; ++i)
        payload += static_cast<char>(i * 7);
    send(exec, payload);
    agent.finish_input(exec);
    std::string exec_output;
    pump_until(
        [&]
        {
            char bytes[1024];
            while (const auto count = agent.read(exec, bytes, sizeof bytes))
                exec_output.append(bytes, count);
            return agent.state(exec) == GuestAgent::State::exited;
        });
    assert(exec_output == payload + "done");
    agent.state(exec, &status);
    assert(status == 4);
    agent.close(exec);
    // A command skips the login profile but must still find the certificates and the user's tools.
    const int setup = agent.open(80, 24, "dumb",
                                 "test -s \"$SSL_CERT_FILE\" && case \":$PATH:\" in "
                                 "*:/mnt/data/bin:*) exit 7 ;; esac; exit 8");
    assert(setup > 0);
    pump_until([&] { return agent.state(setup) != GuestAgent::State::opening; });
    agent.finish_input(setup);
    pump_until([&] { return agent.state(setup) == GuestAgent::State::exited; });
    agent.state(setup, &status);
    assert(status == 7);
    agent.close(setup);
    agent.detach();
    assert(agent.state(second) == GuestAgent::State::gone);
    agent.close(second);
    assert(ps5_linux_session_stop(session));
    ps5_linux_session_destroy(session);
    std::puts("PASS: real guest agent over second console: independent PTYs, sizes, resize, "
              "Ctrl+C, flood, exit status, binary exec with EOF");
}
