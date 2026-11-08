// PS5Term - bounded, secret-free development status. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
namespace term
{
struct DiagnosticSnapshot
{
    const char *build_id = "unknown";
    const char *instance_id = "unknown";
    int pid = 0;
    std::uint64_t monotonic_us = 0;
    bool shell_connected = false;
    bool ssh_requested = false;
};
// Returns zero on invalid input or insufficient output space.
std::size_t serialize_status(const DiagnosticSnapshot &, char *, std::size_t) noexcept;
bool write_status(const DiagnosticSnapshot &,
                  const char *path = "/data/ps5term/status.json") noexcept;
// Opt-in via /data/ps5term/diagnostics.enabled; at most one write per five seconds.
void tick_diagnostics(std::uint64_t now, bool shell_connected, bool ssh_requested) noexcept;
} // namespace term
