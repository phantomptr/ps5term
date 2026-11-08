// PS5Term - PTY allocation and forkpty. GPL-3.0-or-later.
#pragma once
#ifdef __cplusplus
extern "C"
{
#endif

    struct winsize;
    struct termios;

    // Allocate a PTY pair. On success, *master and *slave are set to the
    // master and slave file descriptors. Returns 0 on success, -1 on error.
    // name_buf (if non-NULL, capacity name_cap) receives the slave device path.
    int ps5term_openpty(int *master, int *slave, char *name_buf, unsigned name_cap,
                        const struct termios *termp, const struct winsize *winp);

    // Fork with a PTY. Parent receives child PID and *master is the PTY
    // master fd. Child returns 0, with stdin/stdout/stderr on the slave.
    // Returns -1 on error in the parent (no child created).
    int ps5term_forkpty(int *master, const struct termios *termp, const struct winsize *winp);

#ifdef __cplusplus
}
#endif
