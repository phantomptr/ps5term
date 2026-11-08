# Running PS5 programs in a tab

PS5Term's own shell (the `1 console` tab) can run a PS5 ELF program, such as a homebrew tool or
a menu drawn with ncurses, right in the tab:

```text
/data$ run /data/homebrew/bin/mytool --flag "two words"
```

Its output shows as it runs and what you type goes to it. When it ends, PS5Term prints
`[mytool exited with status 0]` and the prompt returns.

## What you need

- **An ELF loader on port 9021**, as most PS5 homebrew setups already have (elfldr, or a
  loader that includes it). PS5Term cannot start a process itself. It sends a small launcher
  (`ps5term-run`) to the loader, and the launcher starts your program. If nothing answers on
  9021, `run` says so.
- **An ELF program.** Signed `.self` files from games do not run this way, the same as with
  any ELF loader.

## How it behaves

The PS5 has no pseudo-terminal (TTY) for programs, so PS5Term acts as the terminal:

| | |
|---|---|
| Normal output | Typing goes to PS5Term's line editor; **Enter** sends the line |
| Full screen (the program switches screens, as ncurses does) | Each key goes straight to the program, arrows and function keys included |
| **Ctrl+C** or the Interrupt button | Sends SIGINT |
| Closing the tab, or Reconnect | Stops the program |
| Screen size | `COLUMNS` and `LINES` give the tab's size when it starts; it does not follow a later resize |
| `TERM` | `xterm-256color`, with its terminfo entry in `/data/ps5term/terminfo` (`TERMINFO`) |

`run` works in PS5Term's own tabs, not in `ps5term@` SSH sessions.

## For developers: ncurses and termios

Without a TTY, `tcgetattr` and `tcsetattr` fail (ENOTTY), so ncurses cannot switch to
`cbreak()` or `raw()` and holds every key until Enter. Add one file,
[`tooling/run-payload/ps5term_tty.c`](../tooling/run-payload/ps5term_tty.c), to your program and
wrap the two functions when linking:

```sh
prospero-clang -o app.elf app.c ps5term_tty.c -lncursesw \
    -Wl,--wrap=tcgetattr -Wl,--wrap=tcsetattr
```

On a real terminal nothing changes. On a socket (PS5Term, telnet shells) the terminal settings
live in memory, `cbreak()` succeeds and keys arrive one at a time. The file is permissively
licensed so any program can include it.

Tested on a PS5 (13.60) with ncurses 6.4: boxes, colours, arrow keys, Ctrl+C and exit status.

## Why not a real TTY?

The PS5 kernel has no pseudo-terminal driver: `posix_openpt` returns ENOSYS even to payloads.
PS5Term's Linux tabs have real TTYs because they run in PS5Term's own Linux kernel (see
[SUBSYSTEM.md](SUBSYSTEM.md)); programs built for Linux (riscv64) can run there instead.
