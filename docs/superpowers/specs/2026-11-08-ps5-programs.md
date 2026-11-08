# Running PS5 programs in a tab

Status: implemented and verified on the phat (13.60), 2026-11-08. Findings that changed the
design: the PS5 kernel has no PTY even for payloads (`posix_openpt` returns ENOSYS); payloads run
as another user, so the request is 0644 in a 0711 folder; PS5Term must never inject bytes into a
program's output (split escape sequences); ncurses needs `ps5term_tty.c` for cbreak input; and
libvterm 0.3.3's REP armed the pending wrap one column early (patched). User guide:
docs/PS5_PROGRAMS.md.

## Why

People write PS5 homebrew that talks to a terminal: menus drawn with ncurses, tools that print
progress and read keys. Today they run them through a telnet shell such as shsrv, which needs a
computer. PS5Term should run them on the console, in a tab, like a terminal on any computer runs
a program.

## What stops it today

- **The app cannot start a process.** In the app's context `fork`/`rfork` return EPERM (see
  docs/research/2026-11-01-native-capability-results.md). Payload loaders can: elfldr creates a
  process and loads an ELF into it.
- **There is no PTY.** The PS5 kernel exposes no `posix_openpt`, so there is no TTY to give a
  program, no line discipline, no `SIGWINCH`.
- **A PS5 program is an x86-64 FreeBSD-style ELF**, so PS5Term's RISC-V Linux cannot run it.

## Ideas considered

1. **A launcher payload with a socket as the terminal** (chosen). PS5Term sends a small launcher
   ELF to the console's ELF loader (port 9021, already used by most homebrew setups). The
   launcher reads PS5Term's request, connects back over loopback, and starts the program with
   that socket as stdin, stdout and stderr, using shsrv's audited `elfldr_spawn`. A second
   socket carries control: start result, exit status, interrupt, stop.
2. **Run shsrv and drive it over telnet.** Works today for plain output, but shsrv's own
   prompt, telnet framing and line handling get in the way of a full-screen program, and there
   is no way to send the program a signal.
3. **Emulate a TTY in the kernel or by hooking libc in the child.** Needs kernel changes or
   per-program injection; out of scope and fragile.
4. **Linux binaries only.** Already works (environments), but does not help existing PS5 ELFs.

## Without a TTY, PS5Term plays the TTY

A real terminal's kernel does three things a socket does not. PS5Term does them itself:

| A TTY would... | PS5Term instead... |
|---|---|
| Collect a line with echo and Backspace (cooked mode) | Uses its own line editor while the program shows a normal screen; Enter sends the line plus `\n` |
| Pass each key straight through (raw mode, used by ncurses) | Sends keys as they are typed once the program switches to the alternate screen (ncurses does on `initscr`) |
| Turn `\n` into a new line, Ctrl+C into SIGINT, report the size | Treats `\n` as a new line, sends SIGINT through the control socket on Ctrl+C / Interrupt, sets `COLUMNS` and `LINES` at start |

The program also gets `TERM=xterm-256color` and `TERMINFO=/data/ps5term/terminfo`, where PS5Term
installs xterm-256color's terminfo entry (the PS5 has none), so ncurses can find its terminal.

Limits, stated in the docs: the size is fixed at start (no live resize), `tcgetattr` and other
terminal ioctls fail with ENOTTY (most programs carry on; some refuse to start), and Ctrl+C is
always SIGINT, never a key. Only ELF programs (payloads) run, as with any ELF loader; signed
`.self` files from games do not.

## User experience

In a PS5 shell tab:

```text
/data$ run /data/homebrew/bin/mytool --flag
```

`run` starts the program in this tab. Its output appears as it runs; typing goes to it. When it
ends, PS5Term prints `[mytool exited with status 0]` and the PS5 shell prompt returns. Ctrl+C
or the Interrupt button sends SIGINT; closing the tab stops the program. If no ELF loader
answers on 9021, `run` says so and how to start one. `help run` explains the limits.

## Protocol

PS5Term (the app):

1. Listens on 127.0.0.1, an ephemeral port, with a fresh 128-bit token.
2. Writes `/data/ps5term/run/request` (0600):
   `token=…`, `port=…`, `columns=…`, `rows=…`, `cwd=…`, then one `arg=…` per argument
   (the first is the program's path).
3. Sends `/app0/assets/backend/ps5term-run.elf` to 127.0.0.1:9021 and closes.
4. Accepts two connections within 10 s whose first line is `data <token>` and `control
   <token>`; anything else is closed.

The launcher (`ps5term-run`, a payload):

1. Reads and deletes the request, connects twice and sends the two hello lines.
2. Reads the program, sets the environment and directory, and starts it with the data socket
   as its stdio. Sends `started <pid>` or `failed <reason>` on control.
3. Waits. On control `int` it sends SIGINT; on control EOF (tab closed) it sends SIGKILL.
   When the program ends it sends `exited <code>` or `killed <signal>` and exits.

## Implementation

- `tooling/run-payload/ps5term-run.c` + vendored `third_party/shsrv/elfldr.c`, `pt.c`, built
  with the pinned payload SDK by `tools/build-run-payload.sh` into
  `assets/backend/ps5term-run.elf`.
- `src/platform/program_runner.{hpp,cpp}`: the app side (request, listener, sockets,
  control lines), nonblocking, ticked by the tab's connection; the loader send is a callback
  so host tests can stand in a local fake launcher.
- `BuiltinShell` gains `run`, which hands the request to its connection; `Connection` gains a
  program mode (cooked line editor or raw keys, as above).
- Tests: protocol and state machine against a fake launcher on the host; hardware: a small
  test payload that prints, reads a line, then draws a full-screen screen and reads raw keys,
  plus an ncurses program built from PacBrew.
