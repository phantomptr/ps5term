// PS5Term - explicit platform evidence. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace term::platform
{
enum class CapabilityState : std::uint8_t
{
    unknown,
    supported,
    unsupported,
    failed
};
enum class ContextKind : std::uint8_t
{
    app,
    helper,
    reference
};
enum class Operation : std::uint8_t
{
    identity,
    pipe_poll,
    spawn_wait,
    group_signal,
    pty,
    resize,
    descriptor_domain,
    count
};
inline constexpr std::size_t operation_count = static_cast<std::size_t>(Operation::count);
struct ProcessContext
{
    ContextKind kind = ContextKind::app;
    int pid = -1, parent_pid = -1;
    std::uint32_t uid = UINT32_MAX, effective_uid = UINT32_MAX;
    std::uint32_t gid = UINT32_MAX, effective_gid = UINT32_MAX;
};
struct CapabilityResult
{
    Operation operation = Operation::identity;
    CapabilityState state = CapabilityState::unknown;
    int native_error = 0;
    ProcessContext context{};
};
struct CapabilityReport
{
    unsigned schema = 1;
    // Unknown firmware/model/library identities must stay unknown, never inherited from a VM.
    std::uint32_t firmware = 0;
    std::array<char, 64> model{}, library_id{};
    std::array<CapabilityResult, operation_count> results{};
    bool cancelled = false;
};
const char *operation_name(Operation) noexcept;
const char *capability_name(CapabilityState) noexcept;
} // namespace term::platform
