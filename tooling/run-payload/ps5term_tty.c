/* PS5Term - ps5term_tty.c: add this file to a PS5 program that uses ncurses or termios, so
 * it works in a PS5Term tab (or any telnet/socket shell) even though the PS5 has no TTY.
 *
 * Copyright (C) 2026 PhantomPtr. Permission is granted to use, copy, modify and distribute
 * this file for any purpose, with or without fee. (It is the one file programs link in, so
 * it carries no copyleft; the rest of PS5Term is GPL-3.0-or-later.)
 *
 * The PS5 kernel has no pseudo-terminal driver, so a program's stdin/stdout are a socket and
 * tcgetattr/tcsetattr fail with ENOTTY. ncurses then cannot switch to cbreak/raw input and
 * holds every key until Enter. These two functions wrap the C library's: on a real terminal
 * they behave as before; on the standard streams of anything else they keep the terminal
 * settings in memory and report success. PS5Term itself delivers keys one at a time, turns
 * Ctrl+C into SIGINT and gives the size in COLUMNS and LINES.
 *
 * Build: compile it with the program and wrap the two functions (the SDK's libc is static):
 *   prospero-clang -o app.elf app.c ps5term_tty.c \
 *       -Wl,--wrap=tcgetattr -Wl,--wrap=tcsetattr -lncursesw */
#include <string.h>
#include <termios.h>

int __real_tcgetattr(int fd, struct termios *settings);
int __real_tcsetattr(int fd, int when, const struct termios *settings);

static struct termios ps5term_tty_state;
static int ps5term_tty_ready;

/* What a fresh terminal would report: cooked input with echo, LF to CRLF output, 38400 baud. */
static void ps5term_tty_defaults(void)
{
    if (ps5term_tty_ready)
        return;
    memset(&ps5term_tty_state, 0, sizeof ps5term_tty_state);
    ps5term_tty_state.c_iflag = BRKINT | ICRNL | IXON | IMAXBEL;
    ps5term_tty_state.c_oflag = OPOST | ONLCR;
    ps5term_tty_state.c_cflag = CREAD | CS8 | HUPCL;
    ps5term_tty_state.c_lflag = ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL | ICANON | ISIG | IEXTEN;
    ps5term_tty_state.c_cc[VINTR] = 3;
    ps5term_tty_state.c_cc[VQUIT] = 28;
    ps5term_tty_state.c_cc[VERASE] = 127;
    ps5term_tty_state.c_cc[VKILL] = 21;
    ps5term_tty_state.c_cc[VEOF] = 4;
    ps5term_tty_state.c_cc[VSUSP] = 26;
    ps5term_tty_state.c_cc[VMIN] = 1;
    ps5term_tty_state.c_cc[VTIME] = 0;
    ps5term_tty_state.c_ispeed = ps5term_tty_state.c_ospeed = B38400;
    ps5term_tty_ready = 1;
}

int __wrap_tcgetattr(int fd, struct termios *settings)
{
    if (__real_tcgetattr(fd, settings) == 0)
        return 0;
    if (fd < 0 || fd > 2 || !settings)
        return -1;
    ps5term_tty_defaults();
    *settings = ps5term_tty_state;
    return 0;
}

int __wrap_tcsetattr(int fd, int when, const struct termios *settings)
{
    if (__real_tcsetattr(fd, when, settings) == 0)
        return 0;
    if (fd < 0 || fd > 2 || !settings)
        return -1;
    ps5term_tty_state = *settings;
    ps5term_tty_ready = 1;
    return 0;
}
