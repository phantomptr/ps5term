# Standalone SSH service implementation plan

Execute inline, continuing the approved standalone design and the user's instruction
to implement without further questions. Preserve input mappings and phat isolation.

Goal: a bounded app-owned SSH listener and persistent app-owned identity, followed
by native verification. Standard SSH shell/exec compatibility is the baseline for
Warp, Ghostty, iTerm2, Terminal.app, Windows Terminal and other SSH clients.
Client-specific bootstrap scripts require a complete shell; do not claim these
work with the built-in interpreter. Do not acknowledge a PTY without providing one.

Architecture: one owner drives wolfSSH and all connections; no global cwd changes.
Persist identity under /data/ps5term/ssh, with private regular files and a fail-closed
entropy adapter. Reject unsafe existing files rather than importing legacy secrets.
Do not replace working legacy SSH until the new path passes hardware acceptance.

- [x] Add identity storage with generated ECDSA P-256 host key and random password;
      test persistence, unsafe permissions, truncated files and entropy failure.
- [x] Add bounded nonblocking listener lifecycle; host test authenticated exec,
      listener stop closes active sessions, repeated restart preserves identity,
      connection bounds and port conflicts. Use actual OpenSSH as well as Paramiko.
- [x] Cross-compile the same library with native IO/entropy; audit imports and
      integrate behind a build definition before selecting it as default.
- [x] Hardware-test owned listener on phat, verify app context and SSH username;
      preserve a rollback and leave the working app available on failure.
- [x] Add bounded public keys and SFTP; pass host and native phat acceptance.
- [ ] Add settings editing and complete-shell/PTY integration. Each requires its
      own regression tests before selecting the replacement.
- [ ] Extend terminal emulator UI/features, port utilities and validate packaging.

Review focus: existing identity must not rotate silently; failed entropy must not
produce credentials; stop must release every client; port conflicts must not
attach to an unrelated server; client TERM/environment requests must remain bounded.

Research references: docs/OWNED_SSH.md, docs/BASH_PORT.md,
docs/research/2026-11-01-native-capability-results.md. Jails and complete guest OS
remain capability-gated investigations, not features implied by a linked library.

Continuation 2026-11-01: split the remaining SSH work into bounded milestones.
First add optional private authorized_keys (8 KiB, max eight Ed25519 or ECDSA
P-256/P-384 keys). Reject malformed input, options, certificates and unsupported
algorithms instead of silently removing restrictions. Load on service start;
password remains available. Test matching signatures, unknown keys, wrong user,
actual OpenSSH and identity persistence, then native compile and guarded phat
acceptance. SFTP/configuration and full-shell/PTY remain subsequent tasks.
In-place execution preserves this unborn/untracked worktree and input-agent WIP;
checkpoint records serve as the progress ledger. No commits or subagents.

Key enrollment design: ps5upload cannot write the app-private directory. Keep
permissions private and add authenticated exec-only ssh-key-add/ssh-key-remove
commands, using the same bounded parser and atomic 0600 writes. No private-key
import, permission relaxation or external payload. Existing authenticated sessions
remain until disconnected; updated authorization applies to new connections.
Host RED: unknown command exit127. GREEN: ten service interop tests pass.

SFTP continuation: use pinned upstream wolfsftp.c, not a second protocol parser.
Bound READDIR to 32 entries/64 KiB with partial-page failure cleanup; cap per-client
file and directory handles to four each. Use native 64 KiB raw directory buffers
and conservative no-follow metadata fallback when lstat fails. Host and native
OpenSSH/Paramiko 1 MiB transfers, paging, metadata and slow-reader window tests
pass. Fix reproduced EOF under a 32 KiB receive window by retaining upstream's
partial SFTP packet on WS_WINDOW_FULL. Temporary keys/files were removed.
Distribution now follows compiled definitions and omits unused legacy payloads;
package README must describe the selected server's actual port/login/limits.
