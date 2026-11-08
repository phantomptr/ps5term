// PS5Term - the Linux environments the guest offers (ps5term-env). GPL-3.0-or-later.
#include "linux_environments.hpp"
#include "i18n.hpp"

namespace term
{
namespace
{
constexpr LinuxEnvironmentInfo table[linux_environment_count] = {
    {"base", "Base", N_("BusyBox, curl, nano, tmux, htop. Always there: the rescue shell."), 0},
    {"alpine", "Alpine 3.23", N_("apk packages. Small and quick: about 10 MiB."), 64},
    {"ubuntu", "Ubuntu 24.04 LTS", N_("apt packages. About 350 MiB after apt update; slow here."),
     384},
};

// The text after "key": in json, or empty.
std::string_view value(std::string_view json, std::string_view key) noexcept
{
    for (std::size_t at = 0; (at = json.find(key, at)) != std::string_view::npos; at += key.size())
    {
        if (at < 1 || json[at - 1] != '"' || at + key.size() + 1 >= json.size() ||
            json[at + key.size()] != '"' || json[at + key.size() + 1] != ':')
            continue;
        return json.substr(at + key.size() + 2);
    }
    return {};
}
bool number(std::string_view text, std::uint64_t &out) noexcept
{
    std::uint64_t n = 0;
    std::size_t i = 0;
    for (; i < text.size() && i < 19 && text[i] >= '0' && text[i] <= '9'; ++i)
        n = n * 10 + static_cast<unsigned>(text[i] - '0');
    if (!i)
        return false;
    out = n;
    return true;
}
bool string(std::string_view text, std::string_view &out) noexcept
{
    if (text.empty() || text[0] != '"')
        return false;
    const auto end = text.find('"', 1);
    if (end == std::string_view::npos)
        return false;
    out = text.substr(1, end - 1);
    return true;
}
bool lookup(std::string_view name, LinuxEnvironment &out) noexcept
{
    for (unsigned i = 0; i < linux_environment_count; ++i)
        if (name == table[i].name)
        {
            out = static_cast<LinuxEnvironment>(i);
            return true;
        }
    return false;
}
} // namespace

const LinuxEnvironmentInfo &linux_environment_info(LinuxEnvironment environment) noexcept
{
    const auto index = static_cast<unsigned>(environment);
    return table[index < linux_environment_count ? index : 0];
}

bool parse_linux_environments(std::string_view json, LinuxEnvironments &out) noexcept
{
    LinuxEnvironments result;
    std::string_view text;
    if (!string(value(json, "default"), text) || !lookup(text, result.default_environment))
        return false;
    result.data = value(json, "data").substr(0, 4) == "true";
    if (!number(value(json, "disk_kib"), result.disk_kib) ||
        !number(value(json, "used_kib"), result.used_kib))
        return false;
    // Each environment is one {...} object inside "environments":[...].
    auto list = value(json, "environments");
    if (list.empty() || list[0] != '[')
        return false;
    list = list.substr(0, list.find(']'));
    for (std::size_t open; (open = list.find('{')) != std::string_view::npos;)
    {
        const auto close = list.find('}', open);
        if (close == std::string_view::npos)
            return false;
        const auto item = list.substr(open, close - open + 1);
        list = list.substr(close + 1);
        std::string_view name, state;
        LinuxEnvironment environment;
        std::uint64_t kib = 0;
        if (!string(value(item, "name"), name) || !string(value(item, "state"), state) ||
            !number(value(item, "kib"), kib))
            return false;
        if (!lookup(name, environment) || environment == LinuxEnvironment::base)
            continue; // A newer guest may offer more than this app knows.
        const auto index = static_cast<unsigned>(environment);
        result.installed[index] = state == "installed";
        result.kib[index] = kib;
    }
    result.installed[0] = true;
    result.known = true;
    out = result;
    return true;
}
} // namespace term
