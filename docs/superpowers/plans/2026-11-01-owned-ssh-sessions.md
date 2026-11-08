# Owned SSH command-session boundary

Execute inline under the approved standalone design. The user opted out of
questions and asked to keep working. Preserve input/UI files and Pro isolation.

First implement a transport-independent, bounded adapter around BuiltinShell.
Each instance owns its cwd, command state, descriptors and output. SSH shell mode
provides basic line editing; exec mode emits no prompt/echo and reports stdout,
stderr and an exit status separately. Neither mode promises a PTY or subprocess.
Input overflow must reject a whole command rather than execute a truncated one.
Network partial writes must retain unconsumed bytes. EOF drains existing output;
close cancels work and releases resources. No process-global cwd/fd changes.

- [x] Extend BuiltinShell with command output mode, separate error stream and
      deterministic exit status; keep default local behavior unchanged.
- [x] Test and implement shell/exec adapter: fragmented input/CRLF, backpressure,
      interruption, EOF, close, overflow, independent cwd and streaming output.
- [x] Run host suites/sanitizers and native compile. This adapter alone is not a
      network server, authenticated SSH proof or a deployable replacement.
- [ ] Integrate pinned wolfSSH behind owned worker/service lifecycle and config;
      verify authenticated host SSH before phat activation. This is a subsequent
      task, not satisfied by adapter tests.

Progress: the isolated host transport now performs authenticated SSH shell/exec
through wolfSSH directly, without ps5sshd or shsrv. See `docs/OWNED_SSH.md` for
reproduction and the remaining worker/config/native boundaries. The final item
stays open until the actual app service lifetime and phat activation are proven.
