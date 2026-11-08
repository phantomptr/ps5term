// PS5Term - capability evidence never implies permission. GPL-3.0-or-later.
#include "platform/probe.hpp"
#include <cassert>
#include <cerrno>
using namespace term::platform;
struct Fake
{
    int calls = 0, cleaned = 0;
    bool pending = false, cleanup_pending = false;
    Operation cleaned_operation = Operation::count;
    static ProbeStep step(void *p, Operation op, ProcessContext &ctx, std::uint64_t) noexcept
    {
        auto &self = *static_cast<Fake *>(p);
        ++self.calls;
        ctx.pid = 17;
        if (self.pending)
            return {false, CapabilityState::unknown, 0};
        if (op == Operation::pty)
            return {true, CapabilityState::unsupported, ENOSYS};
        return {true, CapabilityState::supported, 0};
    }
    static void cleanup(void *p, Operation op) noexcept
    {
        auto &self = *static_cast<Fake *>(p);
        ++self.cleaned;
        self.cleaned_operation = op;
    }
    static bool drained(void *p, Operation) noexcept
    {
        return !static_cast<Fake *>(p)->cleanup_pending;
    }
};
int main()
{
    ProbeRunner absent;
    absent.start(ContextKind::app, 0);
    for (unsigned i = 0; i < operation_count; ++i)
        absent.tick(i);
    assert(absent.done());
    for (auto &result : absent.report().results)
        assert(result.state == CapabilityState::unknown);
    Fake fake;
    ProbeRunner runner({&fake, Fake::step, Fake::cleanup});
    runner.start(ContextKind::app, 0);
    runner.tick(1);
    assert(fake.calls == 1 && fake.cleaned == 1);
    assert(runner.report().results[0].context.pid == 17);
    assert(runner.report().results[1].state == CapabilityState::unknown);
    for (unsigned i = 1; i < operation_count; ++i)
        runner.tick(i + 1);
    assert(runner.done());
    auto result = runner.report().results[static_cast<unsigned>(Operation::pty)];
    assert(result.state == CapabilityState::unsupported && result.native_error == ENOSYS);
    assert(result.context.kind == ContextKind::app);
    runner.start(ContextKind::helper, 100);
    assert(runner.report().results[0].state == CapabilityState::unknown);
    fake.pending = true;
    runner.tick(101);
    auto before = fake.cleaned;
    runner.cancel();
    assert(runner.done() && runner.report().cancelled && fake.cleaned == before + 1);
    runner.cancel();
    assert(fake.cleaned == before + 1);
    runner.start(ContextKind::reference, 200);
    runner.tick(201);
    runner.tick(2000201);
    assert(runner.report().results[0].state == CapabilityState::failed);
    assert(runner.report().results[0].native_error == ETIMEDOUT);
    assert(runner.report().results[0].context.kind == ContextKind::reference);
    // A backward clock is rejected and cleans any active operation.
    runner.start(ContextKind::reference, 900);
    runner.tick(899);
    assert(runner.done() && runner.report().cancelled);

    // Timeout starts cleanup once, retains ownership while draining, and cannot
    // be mistaken for a complete report or overwritten by another run.
    Fake delayed;
    delayed.pending = true;
    delayed.cleanup_pending = true;
    ProbeRunner asynchronous({&delayed, Fake::step, Fake::cleanup, Fake::drained});
    assert(asynchronous.start(ContextKind::app, 0));
    asynchronous.tick(1);
    asynchronous.tick(2000001);
    assert(delayed.cleaned == 1 && !asynchronous.done());
    assert(!asynchronous.start(ContextKind::helper, 2000002));
    assert(!asynchronous.report().cancelled);
    asynchronous.tick(2000003);
    assert(delayed.cleaned == 1 && delayed.calls == 1);
    delayed.cleanup_pending = false;
    asynchronous.tick(2000004);
    assert(asynchronous.report().results[0].native_error == ETIMEDOUT);
    assert(asynchronous.report().results[1].state == CapabilityState::unknown);
    asynchronous.cancel();
    assert(asynchronous.done());

    // Cancellation also drains before done; repeated cancel never initiates a
    // second cleanup, and the saved active operation is used after index=end.
    delayed.cleanup_pending = true;
    assert(asynchronous.start(ContextKind::app, 3000000));
    asynchronous.tick(3000001);
    const int cleaned = delayed.cleaned;
    asynchronous.cancel();
    asynchronous.cancel();
    assert(!asynchronous.done() && asynchronous.report().cancelled);
    assert(delayed.cleaned == cleaned + 1);
    assert(delayed.cleaned_operation == Operation::identity);
    delayed.cleanup_pending = false;
    asynchronous.tick(3000002);
    assert(asynchronous.done() && delayed.cleaned == cleaned + 1);

    // Even successful steps cannot advance while cleanup still owns resources.
    delayed.pending = false;
    delayed.cleanup_pending = true;
    assert(asynchronous.start(ContextKind::app, 4000000));
    asynchronous.tick(4000001);
    const int calls = delayed.calls;
    asynchronous.tick(4000002);
    assert(delayed.calls == calls);
    delayed.cleanup_pending = false;
    asynchronous.tick(4000003);
    assert(asynchronous.report().results[0].state == CapabilityState::supported);
    asynchronous.tick(4000004);
    assert(delayed.calls == calls + 1);
    asynchronous.cancel();
    assert(asynchronous.done());
}
