// PS5Term - platform-owned child boundary. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
namespace term::platform
{
struct ChildId
{
    std::uint64_t value = 0, generation = 0;
    bool valid() const noexcept
    {
        return value && generation;
    }
};
struct LaunchSpec
{
    const char *program = nullptr;
    std::uint64_t generation = 0;
};
struct LaunchResult
{
    ChildId child{};
    int native_error = 0;
};
enum class ChildPhase
{
    unknown,
    running,
    exited,
    lost,
    failed
};
struct ChildState
{
    ChildPhase phase = ChildPhase::unknown;
    int exit_status = 0, native_error = 0;
};
struct StopResult
{
    bool stopped = false;
    int native_error = 0;
};
enum class IoState
{
    ok,
    would_block,
    eof,
    failed
};
struct IoResult
{
    std::size_t count = 0;
    IoState state = IoState::failed;
    int native_error = 0;
};
enum class ChildSignal
{
    interrupt,
    terminate
};
// All callbacks are nonblocking. IDs are owned opaque identities, not naked PIDs.
// The adapter must validate generation before EVERY operation and reap on exit.
// Reaping retains the output handle until read returns EOF. That final EOF
// releases the handle; stop also releases it when draining is cancelled.
struct LauncherApi
{
    void *context = nullptr;
    LaunchResult (*start)(void *, const LaunchSpec &) noexcept = nullptr;
    ChildState (*poll)(void *, ChildId) noexcept = nullptr;
    StopResult (*stop)(void *, ChildId) noexcept = nullptr;
    IoResult (*write)(void *, ChildId, const char *, std::size_t) noexcept = nullptr;
    IoResult (*read)(void *, ChildId, char *, std::size_t) noexcept = nullptr;
    bool (*resize)(void *, ChildId, unsigned, unsigned) noexcept = nullptr;
    bool (*signal)(void *, ChildId, ChildSignal) noexcept = nullptr;
};
class PlatformLauncher
{
  public:
    explicit PlatformLauncher(LauncherApi api = {}) noexcept : api_(api)
    {
    }
    LaunchResult start(const LaunchSpec &) noexcept;
    ChildState poll(ChildId) noexcept;
    StopResult stop(ChildId) noexcept;
    IoResult write(ChildId, const char *, std::size_t) noexcept;
    IoResult read(ChildId, char *, std::size_t) noexcept;
    bool resize(ChildId, unsigned, unsigned) noexcept;
    bool signal(ChildId, ChildSignal) noexcept;

  private:
    LauncherApi api_;
};
} // namespace term::platform
