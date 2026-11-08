# Independent Linux SSH sessions

The app keeps one guest and one local console. SSH clients must get separate
PTYs inside that guest; their bytes never pass through hvc0. Keep native SSH and
SFTP available independently. Do not expose the guest agent on a network socket.

## Transport and ownership

Use a second virtio console (hvc1) as a binary transport. A small single-threaded
Linux agent owns at most four PTYs and shell process groups. The host owns SSH
authentication, session selection and the emulator worker. No emulator per client,
no PS5 fork/PTY dependency and no host filesystem exposure.

Protocol v1 has a 16-byte network-order header: `P5GA`, version, type, two zero
reserved bytes, nonzero uint32 session ID, uint32 payload size (max 4096).
Session IDs strictly increase over a transport lifetime; exhausted IDs require a
fresh transport. Invalid frames and future IDs terminate the transport. Structurally valid late
input, credit, resize and close for retired IDs are discarded: they can cross
EXIT in flight. OPEN never reuses an ID.
OPEN carries uint16 columns/rows (1..500 / 1..200), then an allowlisted TERM.
OPENED grants the first 4096 input bytes. INPUT consumes that allowance; INPUT_OK
returns credit only after the entire frame reaches the PTY. OUTPUT consumes
per-session output credit, replenished with CREDIT (max total 8192) only as the
SSH client consumes bytes. RESIZE changes guest PTY size; CLOSE kills the shell and ordinary jobs in its Linux session; the agent
acts as a child subreaper to collect adopted jobs. EXIT carries the shell exit status after buffered output drains.

Initial shell TERM allowlist: xterm, xterm-256color, screen, screen-256color,
tmux, tmux-256color, vt100, dumb. All other inherited environment is replaced.
Exec, environment forwarding, terminal modes and signals need explicit protocol
extensions; accepting them without implementing their semantics is forbidden.
Protocol errors close every owned PTY, kill session jobs and reap direct
children. Guest processes can deliberately escape their session; this is
session cleanup, not containment against malicious guest root.

## Incremental delivery / acceptance

1. Host-tested agent + bounded protocol, compile with guest toolchain. Keep it
   outside shipping images until connected and tested end to end.
2. Add separate virtio device and synchronized worker byte queues, verify both
   consoles with the actual guest. Never parse control frames from hvc0.
3. Move guest lifetime into shared app ownership, add independent SSH routing
   and visible settings; stop closes remote guest sessions before joining.
4. Actual OpenSSH/Warp/Ghostty/iTerm2 PTY/resize/slow-reader/disconnect tests,
   phat-only guarded deployment, image/source/license refresh.

First acceptance covers two independent shells, live window size, binary input,
per-session flow control, stale IDs, capacity, malformed/fragmented frames,
transport EOF and process cleanup. macOS is not evidence for Linux guest PTYs;
run the agent acceptance on Linux, then cross-compile RISC-V. Do not change the
known-good installed app for this incomplete bridge.

## Status (2026-11-04)

Steps 1 to 3 are complete and shipping, with step 4's core verified on phat 13.60: two
sessions, resize, Ctrl+C, exit status and binary exec, using Paramiko. Named GUI clients
remain untested. Additions to v1: the agent announces itself with HELLO (type 10, ID
0xffffffff), and the host sends nothing before it, because the device echoes until the
agent makes it raw. EXEC (type 11: columns, rows, TERM length, TERM, command) runs on a
socket pair; INPUT_EOF (type 12) ends its input. EXEC alone may carry up to 16 KiB of
command (other frames stay at 4 KiB). The TERM length byte's high bit (0x80) runs the
command on a PTY instead, for SSH clients that request one (`ssh -t`). A command that
exits with unread input ends only its own session (ECONNRESET is that session's end). A second HELLO means the agent restarted.

## Evidence at the first step (2026-11-03)

Step 1 is implemented outside the shipping guest: `tooling/subsystem/guest-agent`.
Twelve real Linux PTY tests pass under GCC and Clang18 ASan/UBSan (leak checks
enabled), including four simultaneous output producers stopping exactly at their
8192-byte allowances. The existing Buildroot RISC-V64/musl toolchain builds a
static executable. Cross-compilation is not RISC-V runtime proof. CI is wired to
run `tools/test-guest-agent.sh`; no remote CI run is claimed. Steps 2–4 remain.
