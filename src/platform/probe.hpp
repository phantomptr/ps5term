// PS5Term - bounded opt-in probe orchestration. GPL-3.0-or-later.
#pragma once
#include "capabilities.hpp"
namespace term::platform
{
struct ProbeStep
{
    bool complete = false;
    CapabilityState state = CapabilityState::unknown;
    int native_error = 0;
};
// Callbacks must be nonblocking, advance one step, and preserve owned handles
// until cleanup. cleanup initiates release once; optional drained polls whether
// ownership has conclusively ended. No callback means unknown. No syscalls here.
// The adapter owns retained resources. After cancel, tick until done before
// destroying its context; the runner destructor cannot synchronously reap.
struct ProbeApi
{
    void *context = nullptr;
    ProbeStep (*step)(void *, Operation, ProcessContext &, std::uint64_t) noexcept = nullptr;
    void (*cleanup)(void *, Operation) noexcept = nullptr;
    bool (*drained)(void *, Operation) noexcept = nullptr;
};
class ProbeRunner
{
  public:
    explicit ProbeRunner(ProbeApi api = {}) noexcept : api_(api)
    {
    }
    ~ProbeRunner()
    {
        cancel();
    }
    ProbeRunner(const ProbeRunner &) = delete;
    ProbeRunner &operator=(const ProbeRunner &) = delete;
    // A draining run cannot be replaced. Callers must check the return value.
    bool start(ContextKind, std::uint64_t now) noexcept;
    void tick(std::uint64_t now) noexcept;
    void cancel() noexcept;
    bool done() const noexcept
    {
        return index_ >= operation_count && !active_;
    }
    const CapabilityReport &report() const noexcept
    {
        return report_;
    }

  private:
    ProbeApi api_;
    CapabilityReport report_{};
    std::size_t index_ = operation_count;
    bool active_ = false, draining_ = false;
    Operation active_operation_ = Operation::identity;
    std::uint64_t deadline_ = 0, last_ = 0;
    bool cleanup() noexcept;
};
} // namespace term::platform
