# In-app line shell fallback

Execute inline; user explicitly requested autonomous decisions and no questions.
This narrows the standalone foundation's truthful line fallback. Keep native
child/PTY gates open and preserve all existing controller/keyboard/mouse code.

Decision: provide a small owned command interpreter in the app while process
creation is unresolved. A separate helper still cannot launch; embedding the
existing shell wholesale would retain process-global stdin/stdout/cwd assumptions.
Use per-session cwd and bounded output, without global chdir or fd redirection.
This is a basic command interpreter, not sh, a PTY or an OS subsystem.

First commands: help, pwd, cd, echo, id, ls and cat. Reuse the existing bounded
single-command quoting parser. Reject pipelines/redirection/operators explicitly.
ls/cat advance cooperatively with bounded entries/bytes each tick; interruption
closes owned resources. This bounds work, but individual filesystem calls may
still wait on the OS: do not claim hard nonblocking file I/O.

- [x] Tests first: per-session cwd isolation, quoting, invalid commands/operators,
      bounded streaming without truncation, busy/backpressure, interruption,
      file types, descriptor cleanup and actual filesystem errors.
- [x] Implement independent BuiltinShell byte interface and host verification.
- [x] Make Connection use the owned shell by default. Keep legacy loader path
      only in an explicit compile variant. No UI/input routing changes.
- [x] Native build and phat normal deployment/reopen. Check shell output using
      observed controller input; capture build/instance evidence separately.
- [x] Document supported commands and limitations. SSH remains separate legacy
      service until its in-app session adapter is implemented and tested.

Verification: Linux full `make test` passed 44 Python tests, 3 renderer cases and
20 terminal suites; macOS passed the 20 terminal suites. The native-directory
adapter is also compiled on the host against a fake public syscall boundary with
real directory descriptors, including the 64 KiB block requirement and cleanup;
ASAN/UBSAN passed that test. This is separate from physical validation.

On phat 13.60, the installed basic interpreter accepted controller `id`/`help`
and native keyboard commands. Directory changes and reading a bundled manifest
passed. The final directory build listed `/app0` and `/app0/assets`, returned the
prompt, and matched independent build/PID/foreground evidence. See the native
capability decision record for identities and remaining child/PTY/SSH gates.
