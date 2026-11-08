// PS5Term - opt-in native app evidence. GPL-3.0-or-later.
#pragma once
#include "probe.hpp"
#include "native_child.hpp"
#include <optional>
namespace term::platform
{
// Baseline does not spawn. A separately compiled/requested child experiment
// uses only audited ordinary libkernel bindings; neither mode allocates PTYs.
class NativeProbeService
{
  public:
    explicit NativeProbeService(NativeChildCalls calls = native_child_calls()) noexcept;
    void tick(std::uint64_t now, const char *request, const char *report) noexcept;

  private:
    int descriptors_[2] = {-1, -1};
    bool cleanup_ok_ = true, running_ = false;
    bool child_mode_ = false;
    std::uint64_t next_request_ = 0, next_publish_ = 0;
    std::optional<NativeChildProbe> child_;
    NativeChildCalls child_calls_;
    ProbeRunner runner_;
    static ProbeStep step(void *, Operation, ProcessContext &, std::uint64_t) noexcept;
    static void cleanup(void *, Operation) noexcept;
    static bool drained(void *, Operation) noexcept;
    void publish(const char *) noexcept;
};
void tick_native_capabilities(std::uint64_t now) noexcept;
} // namespace term::platform
