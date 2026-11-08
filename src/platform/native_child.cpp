// PS5Term - GPL-3.0-or-later.
#include "native_child.hpp"
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
namespace term::platform
{
namespace
{
constexpr std::size_t stack_size = 65536;
}
NativeChildProbe::~NativeChildProbe()
{
    cleanup();
    // Never free dependent resources while an owned child is still pending.
    // A live service must keep ticking drained() before destroying its context.
    drained();
}
void NativeChildProbe::release() noexcept
{
    for (int &fd : descriptors_)
    {
        if (fd >= 0 && close(fd) < 0)
            cleanup_ok_ = false;
        fd = -1;
    }
    if (stack_ && munmap(stack_, stack_size) < 0)
        cleanup_ok_ = false;
    stack_ = nullptr;
}
void NativeChildProbe::poll() noexcept
{
    if (pid_ <= 0 || reaped_ || lost_)
        return;
    int error = 0;
    int result = calls_.wait(calls_.context, pid_, status_, error);
    if (result == pid_)
        reaped_ = true;
    else if (result != 0 && !(result < 0 && error == EINTR))
    {
        // ECHILD or an unexpected wait result ends permission to signal a PID.
        lost_ = true;
        error_ = error ? error : ECHILD;
        cleanup_ok_ = false;
    }
}
ProbeStep NativeChildProbe::step() noexcept
{
    if (!started_)
    {
        started_ = true;
        if (!calls_.spawn || !calls_.wait || !calls_.stop)
            return {true, CapabilityState::unsupported, ENOSYS};
        stack_ = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (stack_ == MAP_FAILED)
        {
            stack_ = nullptr;
            return {true, CapabilityState::failed, errno};
        }
        if (!calls_.shared_marker && pipe(descriptors_) < 0)
        {
            int e = errno;
            release();
            return {true, CapabilityState::failed, e};
        }
        for (int fd : descriptors_)
        {
            if (fd < 0)
                continue;
            int flags = fcntl(fd, F_GETFL);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            {
                int e = errno;
                release();
                return {true, CapabilityState::failed, e};
            }
        }
        pid_ = calls_.spawn(calls_.context, descriptors_[1], stack_, error_);
        if (pid_ <= 0)
        {
            int e = error_ ? error_ : EIO;
            release();
            return {true,
                    (e == ENOSYS || e == ENOTSUP) ? CapabilityState::unsupported
                                                  : CapabilityState::failed,
                    e};
        }
        if (descriptors_[1] >= 0 && close(descriptors_[1]) < 0)
            cleanup_ok_ = false;
        descriptors_[1] = -1;
        return {};
    }
    if (stopping_)
        return {true, CapabilityState::failed, ECANCELED};
    poll();
    if (lost_)
        return {true, CapabilityState::failed, error_};
    if (!reaped_)
        return {};
    char marker = 0;
    bool data_ok = calls_.shared_marker ? *static_cast<volatile char *>(stack_) == 'r'
                                        : read(descriptors_[0], &marker, 1) == 1 && marker == 'r';
    bool exit_ok = WIFEXITED(status_) && WEXITSTATUS(status_) == 23;
    return {true, data_ok && exit_ok ? CapabilityState::supported : CapabilityState::failed,
            data_ok && exit_ok ? 0 : EIO};
}
void NativeChildProbe::cleanup() noexcept
{
    stopping_ = true;
    // Recheck ownership immediately before a possible signal; never signal after
    // a successful reap or an unexpected wait/ECHILD result.
    poll();
    if (pid_ > 0 && !reaped_ && !lost_ && !signalled_)
    {
        signalled_ = true;
        if (calls_.stop(calls_.context, pid_) != 0)
            cleanup_ok_ = false;
    }
}
bool NativeChildProbe::drained() noexcept
{
    poll();
    if (lost_ || (pid_ > 0 && !reaped_))
        return false;
    release();
    return true;
}
#if defined(PS5TERM_CHILD_RESEARCH)
namespace
{
int child_entry(void *arg) noexcept
{
#if defined(PS5TERM_CHILD_EMPTY_FDS_RESEARCH)
    *static_cast<volatile char *>(arg) = 'r';
    return 23;
#else
    const char marker = 'r';
    return write(*static_cast<int *>(arg), &marker, 1) == 1 ? 23 : 24;
#endif
}
int spawn(void *, int fd, void *stack, int &error) noexcept
{
    *static_cast<int *>(stack) = fd;
    int flags = RFPROC | RFFDG;
#if defined(PS5TERM_CHILD_EMPTY_FDS_RESEARCH)
    flags = RFPROC | RFCFDG | RFMEM;
    *static_cast<volatile char *>(stack) = 0;
#elif defined(PS5TERM_CHILD_SHARED_VM_RESEARCH)
    flags |= RFMEM;
#endif
    int pid = rfork_thread(flags, static_cast<char *>(stack) + stack_size, child_entry, stack);
    error = pid < 0 ? errno : 0;
    return pid;
}
int wait(void *, int pid, int &status, int &error) noexcept
{
    int result = waitpid(pid, &status, WNOHANG);
    error = result < 0 ? errno : 0;
    return result;
}
int stop(void *, int pid) noexcept
{
    return kill(pid, SIGKILL) == 0 ? 0 : errno;
}
} // namespace
NativeChildCalls native_child_calls() noexcept
{
    NativeChildCalls calls{nullptr, spawn, wait, stop};
#if defined(PS5TERM_CHILD_EMPTY_FDS_RESEARCH)
    calls.shared_marker = true;
#endif
    return calls;
}
#else
NativeChildCalls native_child_calls() noexcept
{
    return {};
}
#endif
} // namespace term::platform
