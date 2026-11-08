// PS5Term - GPL-3.0-or-later.
#include "launcher.hpp"
#include <cerrno>
namespace term::platform
{
LaunchResult PlatformLauncher::start(const LaunchSpec &spec) noexcept
{
    if (!spec.program || !*spec.program || !spec.generation)
        return {{}, EINVAL};
    // A partial adapter cannot create children it cannot service or clean up.
    if (!api_.start || !api_.poll || !api_.stop || !api_.read || !api_.write)
        return {{}, ENOSYS};
    return api_.start(api_.context, spec);
}
ChildState PlatformLauncher::poll(ChildId id) noexcept
{
    return id.valid() && api_.poll ? api_.poll(api_.context, id)
                                   : ChildState{ChildPhase::unknown, 0, ENOSYS};
}
StopResult PlatformLauncher::stop(ChildId id) noexcept
{
    return id.valid() && api_.stop ? api_.stop(api_.context, id) : StopResult{false, ENOSYS};
}
IoResult PlatformLauncher::write(ChildId id, const char *data, std::size_t size) noexcept
{
    return id.valid() && api_.write ? api_.write(api_.context, id, data, size)
                                    : IoResult{0, IoState::failed, ENOSYS};
}
IoResult PlatformLauncher::read(ChildId id, char *data, std::size_t size) noexcept
{
    return id.valid() && api_.read ? api_.read(api_.context, id, data, size)
                                   : IoResult{0, IoState::failed, ENOSYS};
}
bool PlatformLauncher::resize(ChildId id, unsigned cols, unsigned rows) noexcept
{
    return id.valid() && api_.resize && api_.resize(api_.context, id, cols, rows);
}
bool PlatformLauncher::signal(ChildId id, ChildSignal signal) noexcept
{
    return id.valid() && api_.signal && api_.signal(api_.context, id, signal);
}
} // namespace term::platform
