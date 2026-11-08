# Linux PTY agent

`agent.c` is PS5Term-owned GPL-3.0-or-later guest code. It creates up to four
independent Linux `/bin/sh` PTYs with a fixed environment and bounded binary I/O.
That environment gives SSH commands, which skip the login profile, the certificate
bundle and `/mnt/data/bin`.
It does not run on the native PS5 or rely on native process creation. The host
SSH server remains responsible for authentication; the agent has no listener,
password database, network access or host filesystem bridge.

The [protocol/design](../../../docs/superpowers/specs/2026-11-03-linux-ssh-agent.md)
specifies framing, credits, session identity, lifecycle and integration gates.
The Linux agent requires `/proc`, mounted devpts, Linux subreaper support and
ordinary process/PTY syscalls. Guest root remains separate from native identity.
Kill/reap covers normal interactive jobs in the shell's session. A process that
deliberately daemonizes into another session can outlive it; this is not a jail.

Run acceptance on Linux (CI runs it under clang sanitizers):

```sh
bash tools/test-guest-agent.sh
```

The test runner creates a private disposable build directory, removes it on exit,
and never connects to a PS5. Tests use real Linux shells/PTYS over local pipes.
They cover fragmentation, four-session capacity, independent environments,
binary I/O, resize, exact per-session output allowance, stalled-reader isolation,
slot reuse with fresh IDs, malformed/partial frames, in-flight messages after
exit, and direct/background process cleanup. Shell tests do execute ordinary
Linux commands; run in the existing development container.

Cross-compile separately with the existing Buildroot toolchain:

```sh
/path/to/output/host/bin/riscv64-buildroot-linux-musl-gcc \
  -std=c11 -Os -Wall -Wextra -Werror -static agent.c -o /tmp/ps5term-agent-riscv64
```

Default stdio transport is for tests. The guest service uses
`ps5term-agent --device /dev/hvc1`, configuring that dedicated device in raw mode.
The guest package and `S38ps5term-agent` run it on the app's second console, and the
SSH `linux` login uses it through `src/platform/guest_agent.cpp`. Exec runs on a socket
pair with exact bytes and a real end of input. Arbitrary environments, terminal-mode
negotiation and signal requests are not implemented; Ctrl+C travels as a PTY byte.
