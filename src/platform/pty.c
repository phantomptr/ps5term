// PS5Term - PTY allocation and forkpty. GPL-3.0-or-later.
#if defined(__linux__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600
#endif
#include "pty.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#if !defined(__linux__)
#include <sys/ttycom.h>
#endif
#include <termios.h>

#ifdef __PROSPERO__
#include <sys/syscall.h>
// posix_openpt may not be in libkernel — call the syscall directly.
static int sys_posix_openpt(int flags)
{
    return (int)syscall(504, flags);
}
#else
static int sys_posix_openpt(int flags)
{
    return posix_openpt(flags);
}
#endif

#ifdef __PROSPERO__
// FreeBSD/PS5: grantpt/unlockpt are no-ops on modern pts implementations.
static int do_grantpt(int fd)
{
    (void)fd;
    return 0;
}
static int do_unlockpt(int fd)
{
    (void)fd;
    return 0;
}
#else
static int do_grantpt(int fd)
{
    return grantpt(fd);
}
static int do_unlockpt(int fd)
{
    return unlockpt(fd);
}
#endif

// Get the slave device path.
static int do_ptsname(int fd, char *buf, unsigned cap)
{
#ifdef __PROSPERO__
    // FreeBSD/PS5: use TIOCGPTN ioctl to get pts number.
    int ptn = -1;
    if (ioctl(fd, TIOCGPTN, &ptn) < 0)
        return -1;
    int n = snprintf(buf, cap, "/dev/pts/%d", ptn);
    if (n < 0 || (unsigned)n >= cap)
    {
        errno = ERANGE;
        return -1;
    }
    return 0;
#else
    // macOS/Linux host: use ptsname().
    (void)cap;
    char *name = ptsname(fd);
    if (!name)
        return -1;
    unsigned len = strlen(name);
    if (len >= cap)
    {
        errno = ERANGE;
        return -1;
    }
    memcpy(buf, name, len + 1);
    return 0;
#endif
}

int ps5term_openpty(int *master, int *slave, char *name_buf, unsigned name_cap,
                    const struct termios *termp, const struct winsize *winp)
{
    int mfd = sys_posix_openpt(O_RDWR | O_NOCTTY);
    if (mfd < 0)
        return -1;

    if (do_grantpt(mfd) < 0 || do_unlockpt(mfd) < 0)
    {
        close(mfd);
        return -1;
    }

    char name[64];
    if (do_ptsname(mfd, name, sizeof name) < 0)
    {
        close(mfd);
        return -1;
    }

    int sfd = open(name, O_RDWR | O_NOCTTY);
    if (sfd < 0)
    {
        close(mfd);
        return -1;
    }

    if (termp)
        tcsetattr(sfd, TCSANOW, termp);
    if (winp)
        ioctl(sfd, TIOCSWINSZ, winp);

    *master = mfd;
    *slave = sfd;
    if (name_buf && name_cap > 0)
    {
        unsigned len = strlen(name);
        if (len >= name_cap)
            len = name_cap - 1;
        memcpy(name_buf, name, len);
        name_buf[len] = '\0';
    }
    return 0;
}

int ps5term_forkpty(int *master, const struct termios *termp, const struct winsize *winp)
{
    int mfd, sfd;
    if (ps5term_openpty(&mfd, &sfd, NULL, 0, termp, winp) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0)
    {
        close(mfd);
        close(sfd);
        return -1;
    }

    if (pid == 0)
    {
        // Child: become session leader, set controlling terminal.
        close(mfd);
        setsid();
        ioctl(sfd, TIOCSCTTY, 0);
        dup2(sfd, STDIN_FILENO);
        dup2(sfd, STDOUT_FILENO);
        dup2(sfd, STDERR_FILENO);
        if (sfd > STDERR_FILENO)
            close(sfd);
        return 0;
    }

    // Parent.
    close(sfd);
    *master = mfd;
    return pid;
}
