// PS5Term - GPL-3.0-or-later.
#include "capabilities.hpp"
namespace term::platform
{
const char *operation_name(Operation op) noexcept
{
    switch (op)
    {
    case Operation::identity:
        return "identity";
    case Operation::pipe_poll:
        return "pipe-poll";
    case Operation::spawn_wait:
        return "spawn-wait";
    case Operation::group_signal:
        return "process-group-signal";
    case Operation::pty:
        return "pty-termios";
    case Operation::resize:
        return "pty-resize";
    case Operation::descriptor_domain:
        return "descriptor-domain";
    default:
        return "unknown";
    }
}
const char *capability_name(CapabilityState state) noexcept
{
    switch (state)
    {
    case CapabilityState::supported:
        return "supported";
    case CapabilityState::unsupported:
        return "unsupported";
    case CapabilityState::failed:
        return "failed";
    default:
        return "unknown";
    }
}
} // namespace term::platform
