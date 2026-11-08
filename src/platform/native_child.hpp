// PS5Term - bounded research child, not a package launcher. GPL-3.0-or-later.
#pragma once
#include "probe.hpp"
namespace term::platform
{
struct NativeChildCalls
{
    void *context = nullptr;
    // The adapter receives a supplied 64 KiB stack. In the empty-FD experiment,
    // fd is -1 and a shared marker lives at the stack base; otherwise use the pipe.
    // A failed spawn must not leave an unreported child.
    int (*spawn)(void *, int, void *, int &) noexcept = nullptr;
    int (*wait)(void *, int, int &, int &) noexcept = nullptr;
    int (*stop)(void *, int) noexcept = nullptr;
    bool shared_marker = false;
};
class NativeChildProbe
{
  public:
    explicit NativeChildProbe(NativeChildCalls calls = {}) noexcept : calls_(calls)
    {
    }
    ~NativeChildProbe();
    NativeChildProbe(const NativeChildProbe &) = delete;
    NativeChildProbe &operator=(const NativeChildProbe &) = delete;
    ProbeStep step() noexcept;
    void cleanup() noexcept;
    bool drained() noexcept;
    bool cleanup_ok() const noexcept
    {
        return cleanup_ok_ && !lost_;
    }
    int pid() const noexcept
    {
        return pid_;
    }

  private:
    NativeChildCalls calls_;
    int descriptors_[2] = {-1, -1}, pid_ = -1, status_ = 0, error_ = 0;
    void *stack_ = nullptr;
    bool started_ = false, reaped_ = false, lost_ = false, stopping_ = false, signalled_ = false;
    bool cleanup_ok_ = true;
    void poll() noexcept;
    void release() noexcept;
};
NativeChildCalls native_child_calls() noexcept;
} // namespace term::platform
