# Embedded Linux reference experiment

> Historical research snapshot, not a current feature or deployment guide.
> Start with [development scope](../DEVELOPMENT_SCOPE.md) and
> [current feature access](../FEATURES.md); retain the dated evidence below.

Status as of 2026-11-03: **experimental native integration boots on phat 13.60**.
Settings starts/stops the bundled modern guest. Actual controller input, kernel
version, pipeline and repeat boot passed; SSH remains native. See
[the current subsystem guide](../SUBSYSTEM.md). The earlier sections below are
chronological research evidence, not the current feature-access instructions.

## Actual results

TinyEMU 2019-12-21 booted the official 2018 RISC-V sample Linux image on both
macOS arm64 and a Linux aarch64 Docker container. The guest identified itself as
Linux `4.15.0-00049-ga3b1e7a-dirty riscv64`. Both runs passed actual shell
functions/conditionals, command substitution, pipelines, redirection, a background
process plus wait, and BusyBox `script` allocating a PTY whose size changed from
25x80 to 31x97. These are guest operations, not PS5 native syscalls.

The sample did not mount devpts at startup. The initial PTY attempt failed with
`can't find free pty`; mounting devpts inside the guest made the test pass. The
first harness also expected a single CR before LF, while this console emitted
multiple CRs. A regression test now accepts those line endings without accepting
the echoed test command as a success marker.

The final local runs reached the shell in approximately 0.22 seconds (macOS) and
0.25 seconds (Linux container). Observed child peak resident memory was about
138 MiB and 88 MiB respectively, with 128 MiB guest RAM configured. Host page
accounting and demand allocation differ; these are not PS5 performance estimates
or hard total-memory guarantees. Each run used one guest, no network, no host
directory share and no KVM, and terminated/reaped its owned process. Root image
SHA-256 was unchanged after each run (TinyEMU's default snapshot mode).

Private reproducible evidence is in `.local/dev/embedded-linux-smoke-{darwin,linux}.json`
and adjacent bounded transcripts. These files identify emulator/config hashes and
explicitly mark PS5 runtime unverified. No SSH credential is used in the guest.

## Reproduce

```sh
python3 -m unittest tests.test_embedded_linux -v
python3 -m tools.dev.embedded_linux --download
python3 -m tools.dev.embedded_linux_smoke
```

`--download` is needed only for missing pinned archives. Existing output directories
are refused; select a fresh `--output` for rebuilding, then pass the same path to
the smoke runner. macOS reference output during this experiment used
`build/embedded-linux-darwin-v2`; Linux used the default `build/embedded-linux-linux`.
Both build tools cap compilation at two workers. Smoke execution has a 60-second
deadline and 2 MiB transcript limit. A separate timeout test confirms cleanup on
an unresponsive guest. Seven focused tooling tests pass on macOS and Linux.

Source archives are fetched from [Fabrice Bellard's TinyEMU project](https://bellard.org/tinyemu/).
Pinned hashes are in `tools/dev/embedded_linux.py`; these are observed hashes of
official HTTPS downloads, not publisher signatures. The upstream source retains
its MIT notices. The small host profile patch disables SDL, networking, remote
filesystems, host directory sharing, x86/KVM and 128-bit RISC-V. The interpreter
is software RISC-V, not an amd64 Linux compatibility layer. The old sample image
is research material and must not be included in a product release.

## Native embedding gates

1. Replace the host CLI's process-global termios/stdin, signal/exit and select-loop
   assumptions with a PS5Term-owned console adapter and bounded input/output
   queues. Guest work must run in bounded interpreter batches on an owned worker;
   it must not block rendering or require the PS5 app to fork.
2. Audit and complete teardown. Upstream `riscv_machine_end` contains an explicit
   `XXX: stop all`; process exit in this host spike supplies isolation and cleanup
   that an in-app worker cannot assume. Prove repeated start/stop and failure
   cleanup under sanitizers before native integration.
3. Bound total heap, guest RAM, block snapshot writes and console queues. Convert
   fatal exits and unchecked allocation failures into recoverable startup/runtime
   errors. A configured RAM size alone is insufficient resource containment.
4. Compile the core against the PS5 SDK; verify timers, file I/O, endian/unaligned
   access, atomics, soft floating point and cancellation on reference systems,
   then phat 13.60. Keep this experiment separate from the working native app
   until those boundaries pass.
5. Build a modern, reproducible minimal RISC-V Linux/BusyBox image with complete
   source/license delivery, terminfo and an update plan. Add virtual networking
   and explicit file exchange later; neither is enabled or verified here.
6. Expose Native and Linux as explicit environments. Linux root is guest root;
   it does not grant native PS5 privileges. Route local and SSH sessions to guest
   PTYs only after lifecycle, resize, signals and disconnect semantics are tested.

This path could provide a full Linux shell without native child creation. It
does not yet establish guest integration, Ubuntu support, FreeBSD jails,
confinement, GUI terminal integration or access to PS5 devices from Linux.

## Follow-up: in-process core implemented

`backend/subsystem` now supplies a C adapter without TinyEMU's host CLI. It boots
the same real guest within the host acceptance process. A tracked, bounded heap
owns RAM, device/CPU graphs and a private writable disk copy. Fatal engine paths
and allocation failures return through a C-only recovery boundary; graph teardown
does not depend on the incomplete upstream destructor. No host child, PTY, signal
handler, network socket or host file is created by this adapter.

Acceptance passes on macOS arm64, macOS x86-64 under installed Rosetta, and Linux
aarch64: five boot/stop cycles; actual shell functions, pipelines, background
process/wait and PTYs; live console resize; 40 initialization allocation cutoffs;
runtime OOM; bounded input and retryable output backpressure; unchanged caller disk image.
Linux ASan leak detection passes. macOS ASan/UBSan passes with leak detection
disabled because Apple's runtime does not support it. Pinned engine sign-extension
idioms require disabling UBSan shift-base checks for engine objects only, with
signed arithmetic explicitly wrapped; the adapter remains instrumented. A Linux
UBSan failure on a null pointer passed to zero-length FDT memcpy was fixed in the
small upstream patch. See `backend/subsystem/README.md` for exact constraints.

Two integration defects were reproduced and corrected before proceeding: the
adapter needs host-loop timer polling to wake WFI, and sending a resize interrupt
before the reference guest initializes its virtio console can stall boot. Sizes
are now deferred until the guest input queue is ready.

The same core compiles as PS5 x86-64 objects using the repo's cached SDK and links
with undefined-symbol checks against its libkernel/libSceLibcInternal stubs. The
x86-64 host tests exercise the compiler-built-in recovery path without a setjmp
import. These are compile/link and reference-host results, **not PS5 runtime
evidence**. The object is neither a payload nor a deployable app; it is excluded
from the current distribution.

Remaining product gates: a Linux terminal session connected to the terminal model, a modern
reproducible guest, and an opt-in phat lifecycle test. Native shell/PTY and named
GUI terminal integrations remain unfinished. Linux guest root has no implied
access to PS5 host files, devices or native privileges.

## Follow-up: owned worker and console backpressure

`linux_session.h` now gives the application synchronized input/output, resize,
status and cancellation around a single engine-owning pthread. Both layers use
fixed byte rings. If a console descriptor will not fit, the virtio patch leaves
it pending and the core pauses execution until a reader makes room. When both
output rings fill, the worker condition-waits rather than spinning. Reads and
stop wake it. Idle guest waits use a sleep hint capped at 10 ms. Explicit stop
joins exactly once even with concurrent stop callers; it also cancels startup
or a worker waiting behind a non-reading client.

The session borrows immutable image buffers until stop/join. The engine heap cap
does not include caller image buffers, either layer's fixed rings, or the native
pthread stack. Guest disk writes remain private and ephemeral. No native network,
filesystem exchange or PTY has been added to this runtime.

A real regression was reproduced before the fix: a guest's `poweroff -f` could
discard the final kernel console output produced in the same interpreter batch.
The worker now enters DRAINING, preserves those bytes, then reports the terminal
state. A reader must continue draining; explicit stop may discard pending I/O.

An intermittent exact-line-count failure had a different cause: the reference
Linux kernel inserted `random: crng init done` inside one output line. Captured
bytes established that the line had been interrupted rather than dropped. Tests
now set the guest console log level before checking exact counts. The core test
withholds reads past 64 KiB, then verifies all 20000 lines through 1 KiB reads;
the worker test withholds reads past both rings and verifies all 12000 lines.

The worker also compiles and links against the public SDK's pthread wrappers.
This is still a link check, not evidence that the guest runs on PS5. No installed
app, SSH credential or console state was changed by these host tests.

Separate macOS ThreadSanitizer acceptance passes for the core and worker,
including concurrent stop callers. macOS x86-64 under Rosetta also passes the
address/undefined sanitizer suites with the worker, exercising the recovery path
used by the native architecture. These checks do not establish absence of every
race or native timing correctness. The initial TSan fixture timed out filling
the queues with two-byte `yes` lines; a longer repeated line exercises the same
byte saturation and stop condition within the bounded test deadline.

## Remaining native evidence

Native child creation remains blocked as recorded in
`2026-11-01-native-capability-results.md`. A read-only heuristic search of the
existing private 13.60 image did not validate a syscall table; candidate layouts
were inconsistent with the expected handlers. It establishes neither PTY presence
nor absence. No new capability-profile launch, kernel change, payload send or
console probe was made for this experiment.
