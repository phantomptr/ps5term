// SPDX-License-Identifier: GPL-3.0-or-later
// Partial I/O, VT replies, bounded work and drain-before-exit regression tests.
#include "terminal_transport.hpp"
#include <algorithm>
#include <cassert>
#include <string>
#include <cstdio>
namespace
{
using namespace term;
using namespace term::platform;
struct Peer
{
    std::string received, output;
    std::size_t write_limit = 0, read_limit = 4096;
    std::size_t write_calls = 0, read_calls = 0;
    bool closed = false, fail_write = false;
};
IoResult write(void *ctx, const char *p, std::size_t size) noexcept
{
    auto &peer = *static_cast<Peer *>(ctx);
    ++peer.write_calls;
    if (peer.fail_write)
        return {0, IoState::failed, 5};
    auto n = std::min(size, peer.write_limit);
    peer.received.append(p, n);
    return {n, n ? IoState::ok : IoState::would_block, 0};
}
IoResult read(void *ctx, char *p, std::size_t size) noexcept
{
    auto &peer = *static_cast<Peer *>(ctx);
    ++peer.read_calls;
    auto n = std::min({size, peer.output.size(), peer.read_limit});
    std::copy_n(peer.output.data(), n, p);
    peer.output.erase(0, n);
    return {n, n ? IoState::ok : peer.closed ? IoState::eof : IoState::would_block, 0};
}
} // namespace
int main()
{
    using namespace term;
    Peer peer;
    TerminalTransport io;
    TerminalModel model(24, 80);
    TerminalTransport::Port port{&peer, write, read};
    assert(io.write("abcdef"));
    io.pump(model, port);
    assert(peer.received.empty());
    peer.write_limit = 2;
    for (int i = 0; i < 5; ++i)
        io.pump(model, port);
    assert(peer.received == "abcdef");
    std::string large(TerminalTransport::input_capacity, 'q');
    peer.write_limit = 0;
    assert(io.write(large));
    assert(!io.write("reject-whole-write"));
    io.pump(model, port);
    peer.write_limit = large.size();
    auto before = peer.received.size();
    io.pump(model, port);
    assert(peer.received.size() - before <= TerminalTransport::pump_budget);
    for (int i = 0; i < 32; ++i)
        io.pump(model, port);
    assert(peer.received == "abcdef" + large);
    // Terminal-generated DSR replies share the retained input path.
    peer.output = "\033[H\033[6n";
    peer.write_limit = 1;
    for (int i = 0; i < 20; ++i)
        io.pump(model, port);
    assert(peer.received.ends_with("\033[1;1R"));
    // A closing peer may still have >1 frame of output to drain.
    peer.output = std::string(50000, 'x') + "\r\nTAIL";
    peer.closed = true;
    io.pump(model, port);
    assert(!io.eof());
    for (int i = 0; i < 20 && !io.eof(); ++i)
        io.pump(model, port);
    assert(io.eof() && !io.failed());
    assert(model.cell(model.cursor().row, 0).codepoints[0] == 'T');
    io.reset();
    peer.closed = false;
    peer.fail_write = true;
    assert(io.write("fail"));
    io.pump(model, port);
    assert(io.failed());
    io.reset();
    assert(!io.failed() && !io.eof());
    // Output can stall on model-generated replies without losing input bytes.
    TerminalModel blocked(24, 80);
    while (blocked.text('a'))
    {
    }
    peer = Peer{};
    peer.output = "\033[6n\r\nPRESERVED";
    io.pump(blocked, port);
    peer.write_limit = 65536;
    for (int i = 0; i < 100; ++i)
        io.pump(blocked, port);
    assert(!io.failed());
    assert(blocked.cell(blocked.cursor().row, 0).codepoints[0] == 'P');
    std::puts("terminal transport tests passed");
}
