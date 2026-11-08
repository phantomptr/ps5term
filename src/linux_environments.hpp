// PS5Term - the Linux environments the guest offers (ps5term-env). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace term
{
// Base is the guest's own system; the others are chroot environments on the data disk.
enum class LinuxEnvironment : unsigned
{
    base,
    alpine,
    ubuntu,
    count
};
constexpr unsigned linux_environment_count = static_cast<unsigned>(LinuxEnvironment::count);

struct LinuxEnvironmentInfo
{
    const char *name;  // ps5term-env's name for it
    const char *title; // shown as is in every language
    const char *about; // English, wrapped in N_() where it is defined
    std::uint32_t need_mib;
};
const LinuxEnvironmentInfo &linux_environment_info(LinuxEnvironment) noexcept;

// What `ps5term-env list --json` reported. known is false until a report parses.
struct LinuxEnvironments
{
    bool known = false, data = false;
    LinuxEnvironment default_environment = LinuxEnvironment::base;
    std::uint64_t disk_kib = 0, used_kib = 0;
    bool installed[linux_environment_count]{};
    std::uint64_t kib[linux_environment_count]{};
};
// Strict enough for the guest's own fixed output; false leaves `out` unchanged.
bool parse_linux_environments(std::string_view json, LinuxEnvironments &out) noexcept;
} // namespace term
