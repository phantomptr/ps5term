// PS5Term - GPL-3.0-or-later.
// FreeBSD reference only. This is not evidence of PS5 support.
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int child(void *opaque)
{
#ifdef PS5TERM_REFERENCE_EMPTY_FDS
    *(volatile char *)opaque = 'r';
    return 23;
#else
    const int fd = *(const int *)opaque;
    const char marker = 'r';
    // No allocation, stdio, locks, or inherited application code in the child.
    return write(fd, &marker, 1) == 1 ? 23 : 24;
#endif
}

static long long milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int main(void)
{
    int fds[2] = {-1, -1}, status = 0, error = 0, reaped = 0, timed_out = 0, close_ok = 1;
    void *stack = NULL;
    if (posix_memalign(&stack, 16, 65536) || pipe(fds)) return 2;
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) < 0) return 2;
    // Optional empty-FD mode uses a shared marker instead of an inherited pipe.
    int flags = RFPROC | RFFDG;
    const char *vm = "copied";
#if defined(PS5TERM_REFERENCE_SHARED_VM) || defined(PS5TERM_REFERENCE_EMPTY_FDS)
    flags |= RFMEM; vm = "shared";
#endif
    int child_fd = fds[1]; // Stable until reap, including in the shared-VM case.
    void *child_arg = &child_fd;
#ifdef PS5TERM_REFERENCE_EMPTY_FDS
    flags = RFPROC | RFCFDG | RFMEM;
    *(volatile char *)stack = 0;
    child_arg = stack;
#endif
    pid_t pid = rfork_thread(flags, (char *)stack + 65536, child, child_arg);
    if (pid < 0) { error = errno; goto done; }
    if (close(fds[1])) close_ok = 0;
    fds[1] = -1;
    const long long start = milliseconds();
    if (start < 0) { kill(pid, SIGKILL); return 2; }
    for (;;)
    {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) { reaped = 1; break; }
        if (result < 0 && errno != EINTR) { error = errno; break; }
        long long now = milliseconds();
        if (now < 0 || now - start >= 2000)
        {
            if (!timed_out) { timed_out = 1; kill(pid, SIGKILL); }
            else if (now < 0 || now - start >= 4000) break;
        }
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
done:;
    char marker = 0;
#ifdef PS5TERM_REFERENCE_EMPTY_FDS
    int data_ok = reaped && *(volatile char *)stack == 'r';
    (void)marker;
#else
    int data_ok = read(fds[0], &marker, 1) == 1 && marker == 'r';
#endif
    int exit_ok = reaped && WIFEXITED(status) && WEXITSTATUS(status) == 23;
    if (close(fds[0])) close_ok = 0;
    if (fds[1] >= 0 && close(fds[1])) close_ok = 0;
    // Retain the child stack until ownership has conclusively ended.
    if (pid < 0 || reaped) free(stack);
    printf("{\"schema\":1,\"context\":\"freebsd-reference\",\"ps5_evidence\":false,"
           "\"mechanism\":\"rfork_thread\",\"vm\":\"%s\",\"fds\":\"%s\",\"child_pid\":%d,"
           "\"native_error\":%d,\"data_ok\":%s,\"exit_ok\":%s,"
           "\"timed_out\":%s,\"cleanup_ok\":%s}\n",
           vm,
#ifdef PS5TERM_REFERENCE_EMPTY_FDS
           "empty",
#else
           "copied",
#endif
           (int)pid, error, data_ok ? "true" : "false", exit_ok ? "true" : "false",
           timed_out ? "true" : "false", (close_ok && (pid < 0 || reaped)) ? "true" : "false");
    return error || !data_ok || !exit_ok || timed_out || !close_ok ? 1 : 0;
}
