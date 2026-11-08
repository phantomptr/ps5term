// PS5Term - GPL-3.0-or-later.
#include "connection.hpp"
#include "terminal_model.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <algorithm>
bool contains(const term::TerminalModel &model, std::string_view value)
{
    auto rows = model.rows();
    auto cols = model.columns();
    for (int r = 0; r < rows; ++r)
    {
        std::string line;
        for (int c = 0; c < cols; ++c)
        {
            auto cell = model.cell(r, c);
            auto cp = cell.codepoints[0];
            line += (cp >= 0x20 && cp <= 0x7e) ? static_cast<char>(cp) : ' ';
        }
        if (line.find(value) != std::string::npos)
            return true;
    }
    auto hs = model.history_size();
    auto hf = model.history_first();
    for (std::uint64_t i = 0; i < hs; ++i)
    {
        std::string line;
        for (int c = 0; c < cols; ++c)
        {
            auto cell = model.history_cell(hf + i, c);
            auto cp = cell.codepoints[0];
            line += (cp >= 0x20 && cp <= 0x7e) ? static_cast<char>(cp) : ' ';
        }
        if (line.find(value) != std::string::npos)
            return true;
    }
    return false;
}
struct AgentWire
{
    std::vector<unsigned char> incoming;
    void frame(unsigned type, unsigned id, std::string_view payload = {})
    {
        unsigned char h[16] = {'P', '5', 'G', 'A', 1, static_cast<unsigned char>(type), 0, 0};
        for (unsigned i = 0; i < 4; ++i)
        {
            h[8 + i] = id >> (24 - 8 * i);
            h[12 + i] = payload.size() >> (24 - 8 * i);
        }
        incoming.insert(incoming.end(), h, h + 16);
        incoming.insert(incoming.end(), payload.begin(), payload.end());
    }
    static std::size_t write(void *, const void *, std::size_t size)
    {
        return size;
    }
    static std::size_t read(void *ctx, void *dst, std::size_t size)
    {
        auto &bytes = static_cast<AgentWire *>(ctx)->incoming;
        size = std::min(size, bytes.size());
        std::copy_n(bytes.data(), size, static_cast<unsigned char *>(dst));
        bytes.erase(bytes.begin(), bytes.begin() + size);
        return size;
    }
};
void borrowed_linux_tabs()
{
    term::platform::GuestAgent agent;
    AgentWire wire;
    agent.attach({&wire, AgentWire::write, AgentWire::read});
    wire.frame(10, 0xffffffffu);
    agent.pump();
    const int ssh = agent.open(80, 24, "xterm");
    assert(ssh == 1);
    term::TerminalModel a(24, 80), b(24, 80);
    auto first = std::make_unique<term::Connection>(&agent);
    term::Connection second(&agent);
    first->tick(a, 1);
    second.tick(b, 1);
    assert(first->connected() && second.connected() && agent.open_sessions() == 3);
    wire.frame(2, 2, std::string_view("\0\0\x10\0", 4));
    wire.frame(2, 3, std::string_view("\0\0\x10\0", 4));
    agent.pump();
    wire.frame(5, 2, "first independent output");
    wire.frame(5, 3, "second independent output");
    agent.pump();
    first->tick(a, 2);
    second.tick(b, 2);
    assert(contains(a, "first independent") && !contains(a, "second independent"));
    assert(contains(b, "second independent") && !contains(b, "first independent"));
    assert(first->submit("echo a") && second.submit("echo b"));
    first.reset();
    assert(agent.alive() && agent.open_sessions() == 2);
    assert(agent.state(ssh) == term::platform::GuestAgent::State::opening);
    second.reconnect();
    second.tick(b, 3);
    assert(second.connected() && agent.open_sessions() == 2);
    assert(agent.open(80, 24, "xterm") && agent.open(80, 24, "xterm"));
    term::Connection full(&agent);
    full.tick(a, 4);
    assert(full.failed() && agent.open_sessions() == 4);
    agent.detach();
    second.tick(b, 5);
    assert(second.failed());
}
int main()
{
    borrowed_linux_tabs();
    term::TerminalModel model(24, 80);
    model.feed("\033[20h"); // LNM mode
    term::Connection connection;
    assert(!connection.connected() && !connection.submit("id"));
    assert(!connection.select_environment(term::Connection::Environment::linux_guest));
    assert(connection.environment() == term::Connection::Environment::console);
    connection.tick(model, 1);
    assert(connection.connected() && !connection.failed());

    if (connection.native())
    {
        assert(contains(model, "Native PTY shell ready"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        connection.tick(model, 2);

        assert(connection.submit("echo conn-test-ok"));
        for (int attempt = 0; attempt < 100 && !contains(model, "conn-test-ok"); ++attempt)
        {
            connection.tick(model, 3 + attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(contains(model, "conn-test-ok"));

        assert(connection.interrupt());

        // Reconnect creates a fresh native shell.
        connection.reconnect();
        connection.tick(model, 4);
        assert(connection.connected());
    }
    else
    {
        assert(contains(model, "no PTY or external loader required"));
        assert(connection.submit("id"));
        assert(!connection.submit("help"));
        connection.tick(model, 2);
        assert(contains(model, "uid="));
        assert(!connection.interrupt());
        assert(connection.submit("help"));
        connection.tick(model, 3);
        assert(contains(model, "Built-in commands:"));

        connection.reconnect();
        connection.tick(model, 4);
        assert(connection.connected());
    }

    // Returning from a raw guest resets LNM. Basic output must restore LF->CRLF.
    {
        term::TerminalModel basic(24, 120);
        basic.feed("\033c");
        int owner = 0;
        term::platform::LauncherApi refused{};
        refused.context = &owner;
        term::Connection fallback(refused);
        fallback.tick(basic, 1);
        assert(!fallback.native());
        assert(basic.cell(1, 0).codepoints[0] == 'B');
    }
    std::puts("built-in connection tests passed");
}
