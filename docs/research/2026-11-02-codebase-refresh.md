# Codebase refresh — 2026-11-02

> Historical research snapshot, not a current feature or deployment guide.
> Start with [development scope](../DEVELOPMENT_SCOPE.md) and
> [current feature access](../FEATURES.md); retain the dated evidence below.

Scope: read current sources, build inputs, local artifacts and prior checkpoint;
run the current host terminal suite. No console connection, deployment, native
rebuild, source-code changes or publication during this review. Source may be
changing in another agent's session; this is a point-in-time inventory.

## Current implementation

- `src/app.*` now owns and renders `TerminalModel` cells, rather than the old
  line-only Screen. Colors, reverse video, history, selection and cursor drawing
  are connected. Controller keyboard, native keyboard and pointer UI remain.
- `src/builtin_shell.*` now implements filesystem commands, text filters and
  system information: grep/head/tail/wc/sort/uniq/tee, mkdir/rmdir/rm/cp/mv/touch/
  chmod/stat, uname/hostname/whoami/date/basename/dirname, in addition to earlier
  help/pwd/cd/echo/id/ls/cat. It adds variables/export/unset, buffered pipelines
  and input/output/append redirection. These are bounded in-process commands,
  not a complete POSIX shell or a port of all Unix utilities.
- `src/platform/pty*`, `privilege.*` and Connection add a native /bin/sh attempt
  before falling back to BuiltinShell. A host PTY path works in today's suite;
  that does not establish PS5 process/PTY availability.
- `src/platform/linux_subsystem.*` loads guest images and wraps the owned Linux
  worker. Connection's call to start it is commented out for crash debugging.
  A separate, previously host-tested `backend/subsystem/LinuxTerminal` adapter
  retains partial transfers and uses VT-generated input, but Connection does
  not use it.
- Owned wolfSSH/wolfSSL server, SFTP, public-key management, password editing and
  hybrid ML-KEM KEX exist. They require the owned build flags. SSH still uses
  `src/ssh/CommandSession` and BuiltinShell, not the new native PTY launcher.
- Existing Bash build is a cross-link/reference audit, not an installed native
  shell. Embedded Linux still needs a modern reproducible release guest.

## Findings, in implementation order

1. **Gate native privilege/process experiments before further deployment.**
   Connection now unconditionally tries the PTY launcher on first tick.
   `pty_launcher.cpp` calls `ps5term_escalate`, whose PS5 implementation attempts
   UID, capability, prison and root-directory changes. This is a substantive
   departure from the prior checkpoint's no-credential/kernel-mutation test
   boundary. Failure partway through does not restore prior values. Review this
   path and initialization separately; a passing host test uses a no-op stub.
   Prior rfork EPERM evidence is not superseded by the presence of this code.

2. **Fix shell composition and data-loss behavior.** A fresh standalone host
   probe of `echo hello | cat > out` produced an empty file and `hello\n` on the
   output channel. `finish()` closes the redirect before advancing the pipeline.
   Existing tests exercise pipes and redirection separately and miss this case.
   Also review the 16 KiB pipeline truncation in `append()` and ignored/partial
   file writes; report limits/errors instead of silently losing data. Parser
   operator adjacency and multi-command behavior need explicit acceptance tests.

3. **Finish terminal transport ownership.** Connection reports native writes
   successful when any bytes were accepted, without retaining the suffix.
   Linux writes can partially commit then report failure. Native polling marks
   a child exited before draining its final output. `model.feed()` consumed-byte
   counts are ignored. Use bounded retained queues and drain-before-exit rules
   consistently; test slow readers/writers, large paste, stop and restart.

4. **Complete VT input and display integration.** The app handwrites escape
   sequences instead of using model key/application modes, and never drains
   `TerminalModel::take_output` for query replies. Native paste bypasses model
   bracketed-paste encoding. No app calls connect mouse reporting or live
   terminal resizing; opening the controller keyboard reduces the visible area
   without changing model/guest dimensions. Non-ASCII cells render as `?`.
   Add end-to-end controller/keyboard/mouse tests for both basic and PTY modes,
   including terminal queries, alternate screen, resize and Unicode rendering.

5. **Make subsystem builds explicit and reproducible.** `tools/build.sh` looks
   for TinyEMU under a particular old generated Darwin build directory and uses
   a `.patched` sentinel. It appends `PS5TERM_LINUX_SUBSYSTEM` *after* source
   compilation and build identity generation, so it does not enable the source
   wrapper or accurately describe that feature in the manifest. Replace this
   implicit cache detection with pinned preparation and an explicit build mode.
   Reconcile LinuxSubsystem with the tested LinuxTerminal adapter. Then prepare
   modern guest sources/licenses/terminfo and test native lifecycle, memory,
   cancellation and persistence. Keep native PS5 and guest permissions explicit.

6. **Unify release profile, artifacts and documentation.** README, HANDOFF and
   several setup docs predate owned SSH and current terminal rendering. A plain
   build still selects legacy SSH. Choose the accepted default deliberately,
   build/package one matching candidate, verify hashes and test on phat only.
   Do not assume the current folder, ZIP or running app match each other.

7. **Product/release work after the above gates.** Real Warp/Ghostty/iTerm2
   interactive acceptance depends on a functioning full shell/PTY, not just SSH
   negotiation. Utilities need a port manifest, dependency/license delivery and
   capability reporting. Tabs/panes/search/font preferences and subsystem Settings
   remain product work. Jail/Linux-ABI/VM support is not established by this
   codebase. Additional firmware remains untested; make unavailable capabilities
   explicit rather than promising every jailbroken firmware. Publish to
   ProsperoStore only after release acceptance and the user's publication step.

## Fresh verification and artifact state

- `TEST_OUTPUT_DIR=/tmp/ps5term-scan-20261102 bash tools/test-terminal.sh`:
  **exit 0**, all 22 test executables ran on macOS. The dedicated
  `test_pty_launcher.cpp` is not wired into that script, although the Connection
  test exercises the host PTY path. This was not a full `make test`, Linux CI,
  sanitizer rerun, native build or physical-device test.
- Extra temporary host probe reproduced pipeline-plus-redirection failure above;
  it created/removed only its own temporary directory. Log:
  `/tmp/ps5term-scan-20261102.log`.
- `dist/FAKE99260/assets/build-info.json`: build `65ab2a2e…`, definitions `[]`.
  Manifest comparison already differs for app.cpp, builtin_shell.cpp and
  platform/pty_launcher.cpp. This is not a current-source release candidate.
- ZIP: build `94da242b…`, owned SSH/SFTP flags present, CRC check passes. It is
  the older checkpointed PQ candidate, not the newer app folder.
- Last checkpoint records phat PQ negotiation success on that older owned
  build. No current live console state was verified in this review.
- No build container was running at inspection. Existing unrelated containers
  were untouched. No VMs started, no console lock acquired, no Pro interaction.

Next implementation batch: gate the automatic native experiment, add/fix the
pipeline-redirection regression and bounded-transfer regressions, then finish VT
input/reply/resize routing before another native candidate is built or deployed.
