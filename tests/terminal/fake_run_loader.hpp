// PS5Term GPL-3.0-or-later. Test helpers for `run`: a fake ELF loader that plays the
// ps5term-run launcher (with the payload's own request parser) and a scripted program.
#pragma once
#include "connection.hpp"
#include "terminal_model.hpp"
#include "../../tooling/run-payload/run_request.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace
{
bool screen_has(const term::TerminalModel &model, std::string_view value)
{
    const auto line_of = [&](auto cell_at)
    {
        std::string line;
        for (int c = 0; c < model.columns(); ++c)
        {
            const auto cp = cell_at(c).codepoints[0];
            line += cp >= 0x20 && cp <= 0x7e ? static_cast<char>(cp) : ' ';
        }
        return line;
    };
    for (int r = 0; r < model.rows(); ++r)
        if (line_of([&](int c) { return model.cell(r, c); }).find(value) != std::string::npos)
            return true;
    for (std::uint64_t i = 0; i < model.history_size(); ++i)
        if (line_of([&](int c) { return model.history_cell(model.history_first() + i, c); })
                .find(value) != std::string::npos)
            return true;
    return false;
}
[[maybe_unused]] void print_screen(const term::TerminalModel &model)
{
    for (int r = 0; r < model.rows(); ++r)
    {
        std::string line;
        for (int c = 0; c < model.columns(); ++c)
        {
            const auto cp = model.cell(r, c).codepoints[0];
            line += cp >= 0x20 && cp <= 0x7e ? static_cast<char>(cp) : ' ';
        }
        if (line.find_first_not_of(' ') != std::string::npos)
            std::fprintf(stderr, "  |%s\n", line.c_str());
    }
}
int listen_any(unsigned &port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof address;
    assert(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    assert(listen(fd, 4) == 0);
    assert(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0);
    port = ntohs(address.sin_port);
    return fd;
}
int connect_to(unsigned port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    return fd;
}
void put(int fd, std::string_view text)
{
    assert(send(fd, text.data(), text.size(), 0) == static_cast<ssize_t>(text.size()));
}
std::string line_from(int fd)
{
    std::string line;
    char ch;
    while (recv(fd, &ch, 1, 0) == 1 && ch != '\n')
        line += ch;
    return line;
}
const std::string launcher_bytes = "\177ELF fake ps5term-run";

// The fake ELF loader: takes the launcher, then acts as it would.
struct FakeLoader
{
    unsigned port = 0;
    int listener = -1;
    std::string request_path;
    std::thread thread;
    std::atomic<bool> got_interrupt{false};
    std::string received_arguments;
    void start(bool interrupt_kills)
    {
        listener = listen_any(port);
        thread = std::thread([this, interrupt_kills] { serve(interrupt_kills); });
    }
    void serve(bool interrupt_kills)
    {
        const int loader = accept(listener, nullptr, nullptr);
        std::string elf;
        char buffer[4096];
        for (ssize_t got; (got = recv(loader, buffer, sizeof buffer, 0)) > 0;)
            elf.append(buffer, static_cast<std::size_t>(got));
        close(loader);
        assert(elf == launcher_bytes);
        // What ps5term-run does: read the request with the same parser.
        struct stat info
        {
        };
        assert(stat(request_path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0644);
        FILE *file = std::fopen(request_path.c_str(), "rb");
        assert(file);
        char text[ps5term_run_max_text + 1];
        const auto size = std::fread(text, 1, sizeof text - 1, file);
        std::fclose(file);
        unlink(request_path.c_str());
        static Ps5termRunRequest request;
        assert(ps5term_run_parse(text, size, &request) == 0);
        assert(request.columns >= 2 && request.rows >= 2 && std::string(request.cwd) == "/tmp");
        for (int i = 0; i < request.argc; ++i)
            received_arguments += std::string(request.argv[i]) + "|";
        // A stranger with the wrong token is turned away.
        const int stranger = connect_to(request.port);
        put(stranger, "data 00000000000000000000000000000000\n");
        const int data = connect_to(request.port);
        put(data, std::string("data ") + request.token + "\n");
        const int control = connect_to(request.port);
        put(control, std::string("control ") + request.token + "\n");
        put(control, "started 42\n");
        // The program: a prompt, a line, then a full screen with raw keys until Ctrl+C.
        put(data, "name? ");
        const auto name = line_from(data);
        put(data, "hi " + name + "\n");
        // One byte at a time, as a real program's output can arrive: a sequence split
        // across reads must still switch screens.
        for (char ch : std::string_view("\033[?1049h\033[2J\033[HFULL SCREEN"))
        {
            put(data, std::string_view(&ch, 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        char key = 0;
        std::string keys;
        while (keys.size() < 3 && recv(data, &key, 1, 0) == 1)
            keys += key;
        put(data, " keys=" + keys);
        if (interrupt_kills)
        {
            assert(line_from(control) == "int");
            got_interrupt = true;
            put(control, "killed 2\n");
        }
        else
            put(control, "exited 5\n");
        close(data);
        close(control);
        close(stranger);
        close(listener);
    }
};

// A launcher API with no functions: the host's own PTY is refused, so the tab runs the
// built-in PS5 shell, as on the console.
int no_pty_owner = 0;
term::platform::LauncherApi no_pty()
{
    term::platform::LauncherApi refused{};
    refused.context = &no_pty_owner;
    return refused;
}
} // namespace
