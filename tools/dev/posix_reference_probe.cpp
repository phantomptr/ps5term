// PS5Term - Linux/FreeBSD/macOS reference only. GPL-3.0-or-later.
// Never compiled into the native app: successful POSIX tests are not PS5 evidence.
#include "platform/probe.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
extern char **environ;
using namespace term::platform;
namespace
{
std::uint64_t now_us()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}
struct Reference
{
    int master = -1, slave = -1;
    pid_t child = -1;
    bool signalled = false, cleanup_failed = false;
    static ProbeStep error(int error) noexcept
    {
        return {true, error == ENOSYS || error == ENOTSUP ? CapabilityState::unsupported : CapabilityState::failed, error};
    }
    static ProbeStep success() noexcept { return {true, CapabilityState::supported, 0}; }
    bool pty() noexcept
    {
        // FreeBSD rejects extra flags here; set nonblocking mode separately.
        master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master < 0) return false;
        const int flags = fcntl(master, F_GETFL);
        if (flags < 0 || fcntl(master, F_SETFL, flags | O_NONBLOCK) < 0 ||
            grantpt(master) < 0 || unlockpt(master) < 0) return false;
        const char *name = ptsname(master);
        if (!name) return false;
        slave = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
        return slave >= 0;
    }
    ProbeStep step(Operation op, ProcessContext &ctx) noexcept
    {
        ctx.pid = getpid(); ctx.parent_pid = getppid();
        ctx.uid = getuid(); ctx.effective_uid = geteuid();
        ctx.gid = getgid(); ctx.effective_gid = getegid();
        switch (op)
        {
        case Operation::identity: return success();
        case Operation::pipe_poll:
        {
            int fds[2];
            if (pipe(fds) < 0) return error(errno);
            master = fds[0]; slave = fds[1];
            if (fcntl(master, F_SETFL, O_NONBLOCK) < 0 || fcntl(slave, F_SETFL, O_NONBLOCK) < 0) return error(errno);
            char value = 'p', out = 0;
            pollfd p{master, POLLIN, 0};
            if (write(slave, &value, 1) != 1 || poll(&p, 1, 0) != 1 || read(master, &out, 1) != 1 || out != value)
                return error(EIO);
            return success();
        }
        case Operation::spawn_wait:
        case Operation::group_signal:
        {
            if (child < 0)
            {
                posix_spawnattr_t attr;
                int rc = posix_spawnattr_init(&attr);
                if (rc) return error(rc);
                if (op == Operation::group_signal)
                {
                    rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
                    if (!rc) rc = posix_spawnattr_setpgroup(&attr, 0);
                }
                char shell[] = "/bin/sh", option[] = "-c";
                char normal[] = "exit 23", group[] = "exec sleep 30";
                char *args[] = {shell, option, op == Operation::spawn_wait ? normal : group, nullptr};
                if (!rc) rc = posix_spawn(&child, shell, nullptr, &attr, args, environ);
                posix_spawnattr_destroy(&attr);
                if (rc) { child = -1; return error(rc); }
                return {};
            }
            if (op == Operation::group_signal && !signalled)
            {
                // POSIX_SPAWN_SETPGROUP above ensures this is only our child's group.
                if (getpgid(child) != child || kill(-child, SIGTERM) < 0) return error(errno ? errno : EINVAL);
                signalled = true;
            }
            int status = 0;
            pid_t pid = waitpid(child, &status, WNOHANG);
            if (!pid) return {};
            if (pid < 0) return error(errno);
            child = -1;
            bool ok = op == Operation::spawn_wait ? WIFEXITED(status) && WEXITSTATUS(status) == 23 :
                WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM;
            return ok ? success() : error(ECHILD);
        }
        case Operation::pty:
        {
            if (!pty()) return error(errno);
            termios term{};
            if (tcgetattr(slave, &term) < 0) return error(errno);
            term.c_lflag &= ~(ICANON | ECHO);
            term.c_cc[VMIN] = 0; term.c_cc[VTIME] = 0;
            if (tcsetattr(slave, TCSANOW, &term) < 0) return error(errno);
            char value = 't', out = 0;
            if (write(master, &value, 1) != 1) return error(errno);
            pollfd p{slave, POLLIN, 0};
            // Reference executable only; 100 ms bound, outside any app render loop.
            if (poll(&p, 1, 100) != 1 || read(slave, &out, 1) != 1 || out != value) return error(EIO);
            return success();
        }
        case Operation::resize:
        {
            if (!pty()) return error(errno);
            winsize requested{41, 119, 0, 0}, actual{};
            if (ioctl(master, TIOCSWINSZ, &requested) < 0 || ioctl(slave, TIOCGWINSZ, &actual) < 0) return error(errno);
            return actual.ws_row == 41 && actual.ws_col == 119 ? success() : error(EIO);
        }
        // No SceNet in a reference VM. Do not infer that native descriptors interoperate.
        default: return {true, CapabilityState::unknown, 0};
        }
    }
    void cleanup() noexcept
    {
        if (master >= 0) close(master);
        if (slave >= 0) close(slave);
        master = slave = -1;
        if (child > 0)
        {
            kill(child, SIGKILL);
            auto deadline = now_us() + 250000;
            int status = 0;
            while (waitpid(child, &status, WNOHANG) == 0)
            {
                if (now_us() >= deadline) { cleanup_failed = true; break; }
                usleep(1000);
            }
            child = -1;
        }
        signalled = false;
    }
    static ProbeStep run(void *p, Operation op, ProcessContext &ctx, std::uint64_t) noexcept
    { return static_cast<Reference *>(p)->step(op, ctx); }
    static void clean(void *p, Operation) noexcept { static_cast<Reference *>(p)->cleanup(); }
};
}
int main()
{
    Reference reference;
    ProbeRunner runner({&reference, Reference::run, Reference::clean});
    runner.start(ContextKind::reference, now_us());
    while (!runner.done()) { runner.tick(now_us()); usleep(1000); }
    std::printf("{\"schema\":1,\"context\":\"reference\",\"ps5_evidence\":false,\"results\":[");
    bool first = true, failed = false;
    for (const auto &r : runner.report().results)
    {
        std::printf("%s{\"operation\":\"%s\",\"state\":\"%s\",\"native_error\":%d,\"pid\":%d,\"uid\":%u,\"euid\":%u}",
            first ? "" : ",", operation_name(r.operation), capability_name(r.state), r.native_error,
            r.context.pid, r.context.uid, r.context.effective_uid);
        first = false; failed |= r.state == CapabilityState::failed;
    }
    std::printf("],\"cleanup_ok\":%s}\n", reference.cleanup_failed ? "false" : "true");
    return failed || reference.cleanup_failed ? 1 : 0;
}
