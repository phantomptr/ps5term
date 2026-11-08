// PS5Term - bounded app-context baseline. GPL-3.0-or-later.
#include "native_probe.hpp"
#if defined(PS5TERM_CAPABILITY_RESEARCH) || defined(PS5TERM_CHILD_RESEARCH) ||                     \
    defined(PS5TERM_HOST_NATIVE_PROBE_TEST)
#include "../build_info.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
namespace term::platform
{
NativeProbeService::NativeProbeService(NativeChildCalls calls) noexcept
    : child_calls_(calls), runner_({this, step, cleanup, drained})
{
}
ProbeStep NativeProbeService::step(void *opaque, Operation op, ProcessContext &ctx,
                                   std::uint64_t) noexcept
{
    auto &self = *static_cast<NativeProbeService *>(opaque);
    ctx.pid = getpid();
    ctx.parent_pid = getppid();
    ctx.uid = getuid();
    ctx.effective_uid = geteuid();
    ctx.gid = getgid();
    ctx.effective_gid = getegid();
    if (op == Operation::identity)
        return {true, ctx.pid > 0 ? CapabilityState::supported : CapabilityState::failed,
                ctx.pid > 0 ? 0 : errno};
    if (op == Operation::spawn_wait && self.child_mode_ && self.child_)
        return self.child_->step();
    if (op != Operation::pipe_poll)
        return {true, CapabilityState::unknown, 0};
    auto failure = []() noexcept -> ProbeStep
    {
        const int error = errno ? errno : EIO;
        return {true,
                (error == ENOSYS || error == ENOTSUP) ? CapabilityState::unsupported
                                                      : CapabilityState::failed,
                error};
    };
    if (pipe(self.descriptors_) < 0)
        return failure();
    for (int fd : self.descriptors_)
    {
        const int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            return failure();
    }
    constexpr char expected = 'p';
    char actual = 0;
    if (write(self.descriptors_[1], &expected, 1) != 1)
        return failure();
    pollfd descriptor{self.descriptors_[0], POLLIN, 0};
    if (poll(&descriptor, 1, 0) != 1 || !(descriptor.revents & POLLIN))
        return failure();
    if (read(self.descriptors_[0], &actual, 1) != 1 || actual != expected)
        return failure();
    return {true, CapabilityState::supported, 0};
}
void NativeProbeService::cleanup(void *opaque, Operation op) noexcept
{
    auto &self = *static_cast<NativeProbeService *>(opaque);
    if (op == Operation::spawn_wait && self.child_mode_ && self.child_)
        self.child_->cleanup();
    for (int &fd : self.descriptors_)
    {
        if (fd >= 0 && close(fd) < 0)
            self.cleanup_ok_ = false;
        fd = -1;
    }
}
bool NativeProbeService::drained(void *opaque, Operation op) noexcept
{
    auto &self = *static_cast<NativeProbeService *>(opaque);
    if (op == Operation::spawn_wait && self.child_mode_ && self.child_)
    {
        bool done = self.child_->drained();
        if (!self.child_->cleanup_ok())
            self.cleanup_ok_ = false;
        return done;
    }
    return true;
}
void NativeProbeService::publish(const char *path) noexcept
{
    char bytes[4096]{};
    const char *child_vm = "copied";
#if defined(PS5TERM_CHILD_SHARED_VM_RESEARCH) || defined(PS5TERM_CHILD_EMPTY_FDS_RESEARCH)
    child_vm = "shared";
#endif
    const char *child_fds = child_calls_.shared_marker ? "empty" : "copied";
    int used = std::snprintf(
        bytes, sizeof(bytes),
        "{\"schema\":1,\"context\":\"app\",\"scope\":\"%s\",\"build_id\":\"%s\",\"complete\":%s,"
        "\"child_pid\":%d,\"child_vm\":\"%s\",\"child_fds\":\"%s\",\"cleanup_ok\":%s,\"results\":[",
        child_mode_ ? "child-experiment" : "baseline-only", term::build_id,
        runner_.done() ? "true" : "false", child_ ? child_->pid() : -1, child_vm, child_fds,
        cleanup_ok_ && runner_.done() ? "true" : "false");
    if (used <= 0)
        return;
    const auto &report = runner_.report();
    for (std::size_t i = 0; i < report.results.size(); ++i)
    {
        const auto &r = report.results[i];
        int n =
            std::snprintf(bytes + used, sizeof(bytes) - static_cast<std::size_t>(used),
                          "%s{\"operation\":\"%s\",\"state\":\"%s\",\"native_error\":%d,\"pid\":%d,"
                          "\"ppid\":%d,\"uid\":%u,\"euid\":%u,\"gid\":%u,\"egid\":%u}",
                          i ? "," : "", operation_name(r.operation), capability_name(r.state),
                          r.native_error, r.context.pid, r.context.parent_pid, r.context.uid,
                          r.context.effective_uid, r.context.gid, r.context.effective_gid);
        if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(bytes) - static_cast<std::size_t>(used))
            return;
        used += n;
    }
    if (static_cast<std::size_t>(used) + 4 >= sizeof(bytes))
        return;
    std::memcpy(bytes + used, "]}\n", 3);
    used += 3;
    char temporary[512]{};
    int n =
        std::snprintf(temporary, sizeof(temporary), "%s.%d.tmp", path, static_cast<int>(getpid()));
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(temporary))
        return;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return;
    // A bounded record; partial writes leave the previous report intact.
    const auto count = write(fd, bytes, static_cast<std::size_t>(used));
    const bool closed = close(fd) == 0;
    if (count == used && closed && rename(temporary, path) == 0)
        return;
    unlink(temporary);
}
void NativeProbeService::tick(std::uint64_t now, const char *request, const char *report) noexcept
{
    if (running_)
    {
        runner_.tick(now);
        if (runner_.done())
        {
            publish(report);
            running_ = false;
        }
        else if (child_mode_ && now >= next_publish_)
        {
            publish(report);
            next_publish_ = now + 1000000;
        }
        return;
    }
    if (now < next_request_)
        return;
    next_request_ = now + 1000000;
    int fd = open(request, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return;
    char command[32]{};
    const auto count = read(fd, command, sizeof(command));
    close(fd);
    unlink(request);
    constexpr char expected[] = "baseline-v1\n";
    bool baseline =
        count == sizeof(expected) - 1 && !std::memcmp(command, expected, sizeof(expected) - 1);
    bool child = false;
#if defined(PS5TERM_CHILD_RESEARCH) || defined(PS5TERM_HOST_CHILD_PROBE_TEST)
    constexpr char child_request[] = "child-v1\n";
    child = count == sizeof(child_request) - 1 &&
            !std::memcmp(command, child_request, sizeof(child_request) - 1);
#endif
    if (!baseline && !child)
        return;
    child_mode_ = child;
    if (child_mode_)
        child_.emplace(child_calls_);
    else
        child_.reset();
    if (runner_.start(ContextKind::app, now))
    {
        cleanup_ok_ = true;
        running_ = true;
    }
}
void tick_native_capabilities(std::uint64_t now) noexcept
{
    static NativeProbeService service;
    service.tick(now, "/data/ps5term/probe.request", "/data/ps5term/capabilities.json");
#if defined(PS5TERM_CHILD_RESEARCH)
    static NativeProbeService child_service;
    child_service.tick(now, "/data/ps5term/child-probe.request",
                       "/data/ps5term/child-capabilities.json");
#endif
}
} // namespace term::platform
#else
namespace term::platform
{
void tick_native_capabilities(std::uint64_t) noexcept
{
}
} // namespace term::platform
#endif
