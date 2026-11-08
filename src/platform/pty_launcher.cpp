// PS5Term - PTY-backed native process launcher. GPL-3.0-or-later.
#include "pty_launcher.hpp"
#if defined(__PROSPERO__) && !defined(PS5TERM_NATIVE_PTY_RESEARCH)
namespace term::platform
{
LauncherApi pty_launcher_create() noexcept
{
    return {};
}
void pty_launcher_destroy(LauncherApi &api) noexcept
{
    api = {};
}
} // namespace term::platform
#else
#include "pty.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace term::platform
{
namespace
{
static constexpr int max_children = 4;

struct Child
{
    pid_t pid = -1;
    int master_fd = -1;
    std::uint64_t generation = 0;
    ChildPhase phase = ChildPhase::unknown;
    int exit_status = 0;
};

struct Context
{
    Child children[max_children]{};
    std::uint64_t next_generation = 1;
};

Child *find(Context *ctx, ChildId id) noexcept
{
    for (auto &c : ctx->children)
        if (c.pid >= 0 && c.generation == id.generation &&
            static_cast<std::uint64_t>(c.pid) == id.value)
            return &c;
    return nullptr;
}

Child *find_free(Context *ctx) noexcept
{
    for (auto &c : ctx->children)
        if (c.pid < 0)
            return &c;
    return nullptr;
}

void reap(Child &c) noexcept
{
    if (c.pid < 0)
        return;
    int status = 0;
    pid_t r = waitpid(c.pid, &status, WNOHANG);
    if (r > 0)
    {
        c.phase = ChildPhase::exited;
        c.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    else if (r < 0 && errno == ECHILD)
    {
        c.phase = ChildPhase::lost;
    }
}

void cleanup(Child &c) noexcept
{
    if (c.master_fd >= 0)
    {
        close(c.master_fd);
        c.master_fd = -1;
    }
    c.pid = -1;
    c.phase = ChildPhase::unknown;
    c.generation = 0;
}

LaunchResult do_start(void *ctx_, const LaunchSpec &spec) noexcept
{
    auto *ctx = static_cast<Context *>(ctx_);

    Child *slot = find_free(ctx);
    if (!slot)
        return {{}, EAGAIN};

    struct winsize ws
    {
    };
    ws.ws_row = 29;
    ws.ws_col = 172;

    int master = -1;
    pid_t pid = ps5term_forkpty(&master, nullptr, &ws);
    if (pid < 0)
        return {{}, errno};

    if (pid == 0)
    {
        const char *program = spec.program;
        if (!program || !*program)
            program = "/bin/sh";
        // The bundled libc.prx exports no setenv, so the environment is passed explicitly.
        const char *envp[] = {"TERM=xterm-256color", "HOME=/root",
                              "PATH=/usr/bin:/bin:/usr/sbin:/sbin:/data/ps5term/bin", "PS5TERM=1",
                              nullptr};
        const char *argv[] = {program, nullptr};
        execve(program, const_cast<char *const *>(argv), const_cast<char *const *>(envp));
        _exit(127);
    }

    // Parent — set master fd nonblocking.
    int flags = fcntl(master, F_GETFL, 0);
    if (flags >= 0)
        fcntl(master, F_SETFL, flags | O_NONBLOCK);

    slot->pid = pid;
    slot->master_fd = master;
    slot->generation = ctx->next_generation++;
    slot->phase = ChildPhase::running;
    slot->exit_status = 0;

    ChildId id{};
    id.value = static_cast<std::uint64_t>(pid);
    id.generation = slot->generation;
    return {id, 0};
}

ChildState do_poll(void *ctx_, ChildId id) noexcept
{
    auto *ctx = static_cast<Context *>(ctx_);
    Child *c = find(ctx, id);
    if (!c)
        return {ChildPhase::unknown, 0, ESRCH};
    if (c->phase == ChildPhase::running)
        reap(*c);
    return {c->phase, c->exit_status, 0};
}

StopResult do_stop(void *ctx_, ChildId id) noexcept
{
    auto *ctx = static_cast<Context *>(ctx_);
    Child *c = find(ctx, id);
    if (!c)
        return {false, ESRCH};

    if (c->phase == ChildPhase::running)
    {
        kill(c->pid, SIGHUP);
        // Give it a moment, then force.
        int status = 0;
        pid_t r = waitpid(c->pid, &status, WNOHANG);
        if (r == 0)
        {
            kill(c->pid, SIGKILL);
            waitpid(c->pid, &status, 0);
        }
        c->phase = ChildPhase::exited;
        c->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    cleanup(*c);
    return {true, 0};
}

IoResult do_write(void *ctx_, ChildId id, const char *data, std::size_t size) noexcept
{
    auto *ctx = static_cast<Context *>(ctx_);
    Child *c = find(ctx, id);
    if (!c || c->master_fd < 0)
        return {0, IoState::failed, ESRCH};
    ssize_t n = write(c->master_fd, data, size);
    if (n > 0)
        return {static_cast<std::size_t>(n), IoState::ok, 0};
    if (n == 0)
        return {0, IoState::eof, 0};
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return {0, IoState::would_block, 0};
    return {0, IoState::failed, errno};
}

IoResult do_read(void *ctx_, ChildId id, char *data, std::size_t size) noexcept
{
    auto *ctx = static_cast<Context *>(ctx_);
    Child *c = find(ctx, id);
    if (!c || c->master_fd < 0)
        return {0, IoState::failed, ESRCH};
    ssize_t n = read(c->master_fd, data, size);
    if (n > 0)
        return {static_cast<std::size_t>(n), IoState::ok, 0};
    if (n == 0)
        return {0, IoState::eof, 0};
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return {0, IoState::would_block, 0};
    // EIO is normal when the child exits and the slave is closed.
    if (errno == EIO)
    {
        reap(*c);
        return {0, IoState::eof, 0};
    }
    return {0, IoState::failed, errno};
}

bool do_resize(void *ctx_, ChildId id, unsigned cols, unsigned rows) noexcept
{
    auto *ctx = static_cast<Context *>(ctx_);
    Child *c = find(ctx, id);
    if (!c || c->master_fd < 0)
        return false;
    struct winsize ws
    {
    };
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    return ioctl(c->master_fd, TIOCSWINSZ, &ws) == 0;
}

bool do_signal(void *ctx_, ChildId id, ChildSignal signal) noexcept
{
    auto *ctx = static_cast<Context *>(ctx_);
    Child *c = find(ctx, id);
    if (!c || c->pid < 0)
        return false;
    int sig = (signal == ChildSignal::interrupt) ? SIGINT : SIGTERM;
    return kill(c->pid, sig) == 0;
}

} // namespace

LauncherApi pty_launcher_create() noexcept
{
    auto *ctx = new (std::nothrow) Context{};
    if (!ctx)
        return {};
    return {
        ctx, do_start, do_poll, do_stop, do_write, do_read, do_resize, do_signal,
    };
}

void pty_launcher_destroy(LauncherApi &api) noexcept
{
    if (!api.context)
        return;
    auto *ctx = static_cast<Context *>(api.context);
    for (auto &c : ctx->children)
    {
        if (c.pid >= 0 && c.phase == ChildPhase::running)
        {
            kill(c.pid, SIGKILL);
            waitpid(c.pid, nullptr, 0);
        }
        cleanup(c);
    }
    delete ctx;
    api = {};
}

} // namespace term::platform
#endif
