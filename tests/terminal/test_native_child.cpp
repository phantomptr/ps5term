// PS5Term - GPL-3.0-or-later.
#include "platform/native_child.hpp"
#include <cassert>
#include <cerrno>
#include <unistd.h>
using namespace term::platform;
struct Fake
{
    int polls = 0, signals = 0, error = 0, spawn_error = 0, status = 23 << 8;
    bool exited = false, shared_marker = false;
    static int spawn(void *p, int fd, void *stack, int &error) noexcept
    {
        const char marker = 'r';
        error = static_cast<Fake *>(p)->spawn_error;
        if (error)
            return -1;
        if (static_cast<Fake *>(p)->shared_marker)
        {
            assert(fd == -1);
            *static_cast<char *>(stack) = marker;
        }
        else
            assert(write(fd, &marker, 1) == 1);
        return 123;
    }
    static int wait(void *p, int pid, int &status, int &error) noexcept
    {
        auto &f = *static_cast<Fake *>(p);
        assert(pid == 123);
        ++f.polls;
        error = f.error;
        status = f.status;
        return error ? -1 : (f.exited ? pid : 0);
    }
    static int stop(void *p, int pid) noexcept
    {
        assert(pid == 123);
        ++static_cast<Fake *>(p)->signals;
        return 0;
    }
    NativeChildCalls calls()
    {
        return {this, spawn, wait, stop, shared_marker};
    }
};
int main()
{
    NativeChildProbe absent;
    auto result = absent.step();
    assert(result.complete && result.state == CapabilityState::unsupported);
    assert(absent.drained());
    Fake denied;
    denied.spawn_error = EPERM;
    NativeChildProbe refusal(denied.calls());
    result = refusal.step();
    assert(result.complete && result.state == CapabilityState::failed &&
           result.native_error == EPERM);
    refusal.cleanup();
    assert(refusal.drained() && refusal.cleanup_ok() && denied.signals == 0);
    Fake ok;
    NativeChildProbe child(ok.calls());
    assert(!child.step().complete && child.pid() == 123);
    ok.exited = true;
    result = child.step();
    assert(result.complete && result.state == CapabilityState::supported);
    child.cleanup();
    assert(child.drained() && child.cleanup_ok() && ok.signals == 0);
    Fake memory;
    memory.shared_marker = true;
    NativeChildProbe shared(memory.calls());
    assert(!shared.step().complete);
    memory.exited = true;
    result = shared.step();
    assert(result.complete && result.state == CapabilityState::supported);
    shared.cleanup();
    assert(shared.drained() && shared.cleanup_ok() && memory.signals == 0);
    Fake delayed;
    NativeChildProbe pending(delayed.calls());
    assert(!pending.step().complete);
    pending.cleanup();
    pending.cleanup();
    assert(delayed.signals == 1 && !pending.drained());
    delayed.exited = true;
    assert(pending.drained() && pending.cleanup_ok());
    assert(delayed.signals == 1);
    Fake failed;
    NativeChildProbe wrong(failed.calls());
    assert(!wrong.step().complete);
    failed.exited = true;
    failed.status = 24 << 8;
    result = wrong.step();
    assert(result.complete && result.state == CapabilityState::failed);
    wrong.cleanup();
    assert(wrong.drained());
    // ECHILD loses permission to signal this numeric PID. Keep the run draining
    // rather than guessing that a positive PID still belongs to this probe.
    Fake lost;
    NativeChildProbe stale(lost.calls());
    assert(!stale.step().complete);
    lost.error = ECHILD;
    result = stale.step();
    assert(result.complete && result.native_error == ECHILD);
    stale.cleanup();
    assert(!stale.drained() && !stale.cleanup_ok() && lost.signals == 0);
}
