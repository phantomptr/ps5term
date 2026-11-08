// PS5Term - a close that does not fit the send queue is retried, never dropped.
// GPL-3.0-or-later.
#include "platform/guest_agent.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using term::platform::GuestAgent;
namespace
{
struct Wire
{
    std::string inbox, sent;
    bool accepting = true;
};
std::size_t wire_write(void *c, const void *data, std::size_t size)
{
    auto &w = *static_cast<Wire *>(c);
    if (!w.accepting)
        return 0;
    w.sent.append(static_cast<const char *>(data), size);
    return size;
}
std::size_t wire_read(void *c, void *data, std::size_t size)
{
    auto &w = *static_cast<Wire *>(c);
    const auto n = std::min(size, w.inbox.size());
    std::memcpy(data, w.inbox.data(), n);
    w.inbox.erase(0, n);
    return n;
}
void put32(std::string &s, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        s.push_back(static_cast<char>((v >> shift) & 0xff));
}
std::string frame(unsigned type, std::uint32_t id, const std::string &payload = {})
{
    std::string f = "P5GA";
    f.push_back(1);
    f.push_back(static_cast<char>(type));
    f.push_back(0);
    f.push_back(0);
    put32(f, id);
    put32(f, static_cast<std::uint32_t>(payload.size()));
    return f + payload;
}
std::string word(std::uint32_t v)
{
    std::string s;
    put32(s, v);
    return s;
}
// Count frames of a type for an ID in the bytes the host sent.
int count(const std::string &sent, unsigned type, std::uint32_t id)
{
    int found = 0;
    for (std::size_t at = 0; at + 16 <= sent.size();)
    {
        const auto *p = reinterpret_cast<const unsigned char *>(sent.data() + at);
        const std::uint32_t fid = (p[8] << 24) | (p[9] << 16) | (p[10] << 8) | p[11];
        const std::uint32_t size = (p[12] << 24) | (p[13] << 16) | (p[14] << 8) | p[15];
        if (p[5] == type && fid == id)
            ++found;
        at += 16 + size;
    }
    return found;
}
constexpr unsigned opened = 2, input = 3, input_ok = 4, close_type = 8, hello = 10;
} // namespace

int main()
{
    Wire wire;
    GuestAgent agent;
    agent.attach({&wire, wire_write, wire_read});
    wire.inbox = frame(hello, 0xffffffffU);
    agent.pump();
    assert(agent.alive());
    const int h = agent.open(80, 24, "xterm");
    assert(h > 0);
    agent.pump();
    wire.inbox = frame(opened, static_cast<std::uint32_t>(h), word(4096));
    agent.pump();
    assert(agent.state(h) == GuestAgent::State::running);
    agent.pump(); // flush the initial output credit

    // Stall the wire and fill the send queue with input until a close frame cannot fit.
    wire.accepting = false;
    const std::size_t capacity = GuestAgent::send_queue;
    for (unsigned guard = 0; guard < 64; ++guard)
    {
        const auto room = capacity - agent.queued_bytes();
        if (room < 16)
            break;
        const std::size_t chunk = room >= 16 + 4096 + 16 ? 4096 : room - 16 - 8;
        if (!chunk)
            break;
        assert(agent.write(h, std::string(chunk, 'x')) == chunk);
        const auto before = agent.queued_bytes();
        agent.pump();
        assert(agent.queued_bytes() == before + 16 + chunk);
        wire.inbox = frame(input_ok, static_cast<std::uint32_t>(h), word(0));
        agent.pump();
    }
    assert(capacity - agent.queued_bytes() < 16);
    agent.close(h);
    assert(agent.state(h) == GuestAgent::State::gone);
    // The pending close still holds a guest slot: only three more sessions fit.
    wire.accepting = true;
    agent.pump(); // drains the queue; the close is retried on the next pass
    agent.pump();
    assert(count(wire.sent, close_type, static_cast<std::uint32_t>(h)) == 1);
    assert(count(wire.sent, input, static_cast<std::uint32_t>(h)) > 0);
    agent.pump();
    assert(count(wire.sent, close_type, static_cast<std::uint32_t>(h)) == 1);

    // While a close is pending, opening beyond the guest's four slots is refused.
    const int a = agent.open(80, 24, "xterm");
    assert(a > 0);
    agent.pump();
    wire.inbox = frame(opened, static_cast<std::uint32_t>(a), word(4096));
    agent.pump();
    agent.pump();
    wire.accepting = false;
    for (unsigned guard = 0; guard < 64 && capacity - agent.queued_bytes() >= 16; ++guard)
    {
        const auto room = capacity - agent.queued_bytes();
        const std::size_t chunk = room >= 16 + 4096 + 16 ? 4096 : room - 16 - 8;
        if (!chunk)
            break;
        assert(agent.write(a, std::string(chunk, 'y')) == chunk);
        agent.pump();
        wire.inbox = frame(input_ok, static_cast<std::uint32_t>(a), word(0));
        agent.pump();
    }
    agent.close(a); // queued for retry
    wire.accepting = true;
    agent.pump(); // drain only
    int opened_count = 0;
    for (int i = 0; i < 4; ++i)
        opened_count += agent.open(80, 24, "xterm") > 0;
    assert(opened_count == 3);
    std::puts("guest agent close retry: ok");

    // A long SSH command (Warp's bootstrap is several KiB) leaves as one EXEC frame;
    // the TERM length byte's high bit asks for a PTY.
    {
        Wire w;
        GuestAgent g;
        g.attach({&w, wire_write, wire_read});
        w.inbox = frame(hello, 0xffffffffU);
        g.pump();
        const std::string command(12000, 'c');
        const int pty = g.open(100, 30, "xterm-256color", command, true);
        const int raw = g.open(100, 30, "xterm-256color", "true", false);
        assert(pty > 0 && raw > 0);
        assert(!g.open(80, 24, "xterm", std::string(GuestAgent::max_command + 1, 'c'), true));
        g.pump();
        const auto *p = reinterpret_cast<const unsigned char *>(w.sent.data());
        const std::uint32_t size = (p[12] << 24) | (p[13] << 16) | (p[14] << 8) | p[15];
        assert(p[5] == 11 && size == 5 + 14 + command.size());
        assert(p[16 + 4] == (0x80 | 14) &&
               w.sent.compare(16 + 5 + 14, command.size(), command) == 0);
        const auto *q = p + 16 + size;
        assert(q[5] == 11 && q[16 + 4] == 14); // no PTY flag
        std::puts("guest agent long command and PTY flag: ok");
    }
    return 0;
}
