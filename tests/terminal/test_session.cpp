// PS5Term - session ownership, bounds and partial I/O. GPL-3.0-or-later.
#include "session.hpp"
#include <cassert>
#include <cstring>
using namespace term;
struct Fake
{
    unsigned starts = 0, stops = 0;
    std::size_t written = 0;
    bool block = false, lost = false, exited = false;
    std::size_t output_left = 0;
    static platform::LaunchResult start(void *p, const platform::LaunchSpec &s) noexcept
    {
        auto &f = *static_cast<Fake *>(p);
        ++f.starts;
        return {{f.starts, s.generation}, 0};
    }
    static platform::ChildState poll(void *p, platform::ChildId) noexcept
    {
        auto &f = *static_cast<Fake *>(p);
        if (f.lost)
            return {platform::ChildPhase::lost, 0, 0};
        return {f.exited ? platform::ChildPhase::exited : platform::ChildPhase::running, 23, 0};
    }
    static platform::StopResult stop(void *p, platform::ChildId) noexcept
    {
        ++static_cast<Fake *>(p)->stops;
        return {true, 0};
    }
    static platform::IoResult write(void *p, platform::ChildId, const char *,
                                    std::size_t n) noexcept
    {
        auto &f = *static_cast<Fake *>(p);
        if (f.block)
            return {0, platform::IoState::would_block, 0};
        n = n < 3 ? n : 3;
        f.written += n;
        return {n, platform::IoState::ok, 0};
    }
    static platform::IoResult read(void *p, platform::ChildId, char *out, std::size_t size) noexcept
    {
        auto &f = *static_cast<Fake *>(p);
        if (!f.output_left)
            return {0, f.exited ? platform::IoState::eof : platform::IoState::would_block, 0};
        size = size < f.output_left ? size : f.output_left;
        std::memset(out, 'o', size);
        f.output_left -= size;
        return {size, platform::IoState::ok, 0};
    }
    static bool resize(void *, platform::ChildId, unsigned, unsigned) noexcept
    {
        return true;
    }
    static bool signal(void *, platform::ChildId, platform::ChildSignal) noexcept
    {
        return true;
    }
};
int main()
{
    Fake fake;
    platform::PlatformLauncher launcher({&fake, Fake::start, Fake::poll, Fake::stop, Fake::write,
                                         Fake::read, Fake::resize, Fake::signal});
    SessionManager sessions(launcher);
    auto first = sessions.open("/owned/shell", 0);
    assert(first.valid());
    assert(sessions.write(first, "hello world", 11) == SessionResult::ok);
    sessions.poll(1);
    assert(fake.written == 3); // one bounded write call/session/tick
    sessions.poll(2);
    sessions.poll(3);
    sessions.poll(4);
    assert(fake.written == 11);
    fake.block = true;
    static char large[session_queue_size]{};
    assert(sessions.write(first, large, sizeof(large)) == SessionResult::ok);
    assert(sessions.write(first, "x", 1) == SessionResult::backpressure);
    sessions.poll(5);
    assert(sessions.pending_write(first) == sizeof(large));
    assert(sessions.resize(first, 0, 40) == SessionResult::invalid);
    assert(sessions.resize(first, 120, 40) == SessionResult::ok);
    assert(sessions.signal(first, platform::ChildSignal::interrupt) == SessionResult::ok);
    assert(sessions.close(first) == SessionResult::ok && fake.stops == 1);
    assert(sessions.close(first) == SessionResult::stale && fake.stops == 1);
    auto second = sessions.open("/owned/shell", 10);
    assert(second.slot == first.slot && second.generation != first.generation);
    assert(sessions.write(first, "bad", 3) == SessionResult::stale);
    fake.lost = true;
    sessions.poll(11);
    assert(sessions.status(second).phase == SessionPhase::lost);
    assert(fake.stops == 1); // lost/reused child identity is never killed
    fake.lost = false;
    auto third = sessions.open("/owned/shell", 20);
    fake.exited = true;
    sessions.poll(21);
    assert(sessions.status(third).phase == SessionPhase::exited);
    assert(sessions.status(third).exit_status == 23);
    fake.exited = false;
    auto fourth = sessions.open("/owned/shell", 30);
    sessions.poll(10000030);
    assert(sessions.status(fourth).phase == SessionPhase::expired);
    assert(fake.stops == 2);
    auto drain = sessions.open("/owned/shell", 10000031);
    fake.output_left = 2 * session_read_budget + 123;
    fake.exited = true;
    sessions.poll(10000032);
    assert(fake.output_left == session_read_budget + 123);
    sessions.poll(10000033);
    sessions.poll(10000034);
    assert(fake.output_left == 0);
    static char output[2 * session_read_budget + 123];
    assert(sessions.read(drain, output, sizeof(output)) == sizeof(output));
    for (char ch : output)
        assert(ch == 'o');
    assert(sessions.status(drain).exit_status == 23);
    assert(sessions.close(drain) == SessionResult::ok);
    assert(sessions.close(drain) == SessionResult::stale);
    platform::PlatformLauncher unavailable;
    SessionManager missing(unavailable);
    assert(!missing.open("/owned/shell", 0).valid());
}
