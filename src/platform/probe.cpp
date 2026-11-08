// PS5Term - GPL-3.0-or-later.
#include "probe.hpp"
#include <cerrno>
namespace term::platform
{
bool ProbeRunner::cleanup() noexcept
{
    if (!active_)
        return true;
    if (!draining_)
    {
        draining_ = true;
        if (api_.cleanup)
            api_.cleanup(api_.context, active_operation_);
    }
    if (api_.drained && !api_.drained(api_.context, active_operation_))
        return false;
    active_ = draining_ = false;
    return true;
}
void ProbeRunner::cancel() noexcept
{
    if (done())
        return;
    report_.cancelled = true;
    index_ = operation_count;
    cleanup();
}
bool ProbeRunner::start(ContextKind kind, std::uint64_t now) noexcept
{
    if (draining_)
        return false;
    cancel();
    if (!done())
        return false;
    report_ = {};
    for (std::size_t i = 0; i < operation_count; ++i)
    {
        report_.results[i].operation = static_cast<Operation>(i);
        report_.results[i].context.kind = kind;
    }
    index_ = 0;
    last_ = now;
    deadline_ = 0;
    return true;
}
void ProbeRunner::tick(std::uint64_t now) noexcept
{
    if (done())
        return;
    // Cleanup keeps its original operation even when cancel moved index=end.
    // A backwards clock cannot abandon cleanup or restart another operation.
    if (draining_)
    {
        if (cleanup() && !report_.cancelled)
            ++index_;
        return;
    }
    if (now < last_)
    {
        cancel();
        return;
    }
    last_ = now;
    auto &result = report_.results[index_];
    if (!api_.step || !api_.cleanup)
    {
        ++index_;
        return;
    }
    if (!active_)
    {
        active_ = true;
        active_operation_ = result.operation;
        deadline_ = now + 2000000;
    }
    ProbeStep step;
    if (now >= deadline_)
        step = {true, CapabilityState::failed, ETIMEDOUT};
    else
        step = api_.step(api_.context, result.operation, result.context, now);
    if (!step.complete)
        return;
    result.state = step.state;
    result.native_error = step.native_error;
    if (cleanup())
        ++index_;
}
} // namespace term::platform
