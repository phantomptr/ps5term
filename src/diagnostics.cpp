// PS5Term - GPL-3.0-or-later.
#include "diagnostics.hpp"
#include "build_info.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
namespace term
{
namespace
{
bool identifier(const char *value) noexcept
{
    if (!value || !*value)
        return false;
    for (std::size_t i = 0; i < 96; ++i)
    {
        const char ch = value[i];
        if (!ch)
            return true;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_'))
            return false;
    }
    return false;
}
} // namespace
std::size_t serialize_status(const DiagnosticSnapshot &state, char *out,
                             std::size_t capacity) noexcept
{
    if (!out || !capacity || !identifier(state.build_id) || !identifier(state.instance_id) ||
        state.pid <= 0)
        return 0;
    int n = std::snprintf(
        out, capacity,
        "{\"schema\":1,\"title_id\":\"FAKE99260\",\"build_id\":\"%s\","
        "\"instance_id\":\"%s\",\"pid\":%d,\"monotonic_us\":%llu,"
        "\"terminal_backend\":\"line\",\"shell_connected\":%s,\"ssh_requested\":%s}\n",
        state.build_id, state.instance_id, state.pid,
        static_cast<unsigned long long>(state.monotonic_us),
        state.shell_connected ? "true" : "false", state.ssh_requested ? "true" : "false");
    return n > 0 && static_cast<std::size_t>(n) < capacity ? static_cast<std::size_t>(n) : 0;
}
bool write_status(const DiagnosticSnapshot &state, const char *path) noexcept
{
    char data[1024]{};
    auto count = serialize_status(state, data, sizeof(data));
    if (!count || !path)
        return false;
    char temporary[512]{};
    int n = std::snprintf(temporary, sizeof(temporary), "%s.%s.tmp", path, state.instance_id);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(temporary))
        return false;
    // A leftover temp file is a failed write, not permission to follow/overwrite it.
    int fd = ::open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return false;
    std::size_t offset = 0;
    bool ok = true;
    unsigned retries = 0;
    while (offset < count)
    {
        auto wrote = ::write(fd, data + offset, count - offset);
        if (wrote > 0)
            offset += static_cast<std::size_t>(wrote);
        else if (wrote < 0 && errno == EINTR && retries++ < 8)
            continue;
        else
        {
            ok = false;
            break;
        }
    }
    if (::close(fd) < 0)
        ok = false;
    if (ok && ::rename(temporary, path) == 0)
        return true;
    ::unlink(temporary);
    return false;
}
void tick_diagnostics(std::uint64_t now, bool shell_connected, bool ssh_requested) noexcept
{
    static std::uint64_t next = 0;
    static char instance[64]{};
    if (now < next)
        return;
    next = now + 5000000;
    int enabled = ::open("/data/ps5term/diagnostics.enabled", O_RDONLY);
    if (enabled < 0)
        return;
    ::close(enabled);
    const int pid = static_cast<int>(::getpid());
    if (!instance[0])
        std::snprintf(instance, sizeof(instance), "%x-%llx", static_cast<unsigned>(pid),
                      static_cast<unsigned long long>(now));
    write_status({build_id, instance, pid, now, shell_connected, ssh_requested});
}
} // namespace term
