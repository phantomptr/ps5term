# Native capability decision record — 2026-11-01

> Historical research snapshot, not a current feature or deployment guide.
> Start with [development scope](../DEVELOPMENT_SCOPE.md) and
> [current feature access](../FEATURES.md); retain the dated evidence below.

## Physical evidence

The phat CFI-1115A 01X at `192.168.86.99`, running the user-reported 13.60 jailbreak environment, was the only console tested. Pro was not contacted. The hardware endpoint reports a zero firmware word; the ps5upload UI reports 13.60. No other firmware is validated.

The `PS5TERM_CAPABILITY_RESEARCH` variant was built in the Linux build container, packaged as native title `FAKE99260`, and installed through ps5upload after a complete verified backup. Both candidate transfer and installed files were checked by hash and EOF. The FSELF integrity check passed; eboot digest was `e852beb49ceb4d4c5ba432ab68da6c2e465f8a79a819abb19afecb1f79412bee`.

Source/configuration identity: `1544c3558fbecddf9a0af239506f9fc422f05006d24669ffa9795a806fef7ea2`. Subsequent development-tool edits change the identity of a future rebuild; they do not change this installed artifact. This identity includes compile definitions and public source inputs, but is not a claim of identical toolchains or bit-for-bit reproducibility.

After observing PS5Term selected on the live Remote Play home screen, normal controller confirm launched it. Independent process and focus endpoints matched its diagnostic PID/build record. The app was stopped using its freshly verified title/PID, then reopened through the same observed UI route. The second instance also matched the installed build and foreground focus.

| Measurement | First launch | Second launch |
| --- | --- | --- |
| PID | 194 | 203 |
| Parent PID | 52 | 52 |
| Foreground app ID | 16408 | 24600 |
| UID / effective UID | 1 / 1 | 1 / 1 |
| GID / effective GID | 1 / 1 | 1 / 1 |
| Distinct instance ID | yes | yes |
| Native pipe, nonblocking fcntl, write, poll, read | supported | supported |
| Probe descriptor cleanup | passed | passed |

The native app is not running as root in these measurements. This says nothing about credentials of the existing separate SSH helper, nor does UID alone establish the full console permission model.

Private evidence: `.local/dev/phat-baseline-capture.json`, `.local/dev/phat-reopen-capture.json`, and `.local/dev/activation/aef118f4b6434473a3be80de25817078/activation.json`. The adjacent `known-good/` directory contains the verified pre-update installation. Do not publish private capture directories or SSH data.

## What remains unknown

Native spawn/wait, process-group signaling, PTY/termios, PTY resize and SceNet/kernel descriptor interoperability were deliberately not attempted in this baseline. Their recorded state is `unknown`, with no inference from reference-machine success. The probe does not create processes, invoke arbitrary ioctls, change credentials, inject system processes or modify the kernel.

Reference probes passed identity, pipe/poll, spawn/wait, owned process-group signaling, PTY/termios and resize on macOS, Linux and FreeBSD. They validate the reference harness only.

The app's legacy line shell still connects successfully. Its external-loader bootstrap and existing ps5sshd-derived adapter remain in place. The portable session/protocol foundation is not wired into a native launcher. `ssh_requested:true` only records the setting; it is not a fresh authenticated SSH health check. App stop/reopen here does not prove legacy helper cleanup after app death.

## SDK binding finding

Inspection of the exact local native SDK archive with `llvm-nm-18` and `llvm-objdump-18` found `fork` and `posix_openpt` definitions in `libc.a(syscalls.o)`. They are direct syscall sequences using numbers 2 and 504 respectively, followed by `ret`; the inspected sequences do not translate the carry/error convention into POSIX `-1` and `errno`.

The SDK's separate `libkernel_sys.so` import stub also declares `fork`, `vfork`
and `sceKernelSpawn`, unlike the ordinary `libkernel.so` used by this app. Those
are declarations for a different module/context, not proof the native app can
load or use it. This is a concrete offline investigation lead: check the supplied
13.60 system module and normal-app loading/import rules before choosing a binding.
No system-module load or system-context transition was attempted.

These are not equivalent to verified ordinary libkernel imports. Merely compiling against the SDK headers, or finding these symbols in the archive, does not establish that the PS5 application can use them safely or interpret errors correctly. No live call to these stubs was made. Missing ordinary imports also do not prove the kernel lacks a feature.

The offline elfldr reference uses a substantially different context: `rfork_thread`, target-process bootstrap/exec, ptrace and kernel-assisted preparation. Its initial bootstrap alters privileges and injects a system process. Transplanting it is not a routine native-app spawn adapter and was not attempted in this test.

## Adapter decisions

**Gate A: not passed.** App-owned helper creation/data/exit/cleanup without an external loader has not been demonstrated. Keep the native launcher explicitly unsupported until an audited mechanism works in the actual app context. Never silently select an engine launcher with ShellUI injection.

**Gate B: not passed.** Native PTY allocation, controlling-terminal semantics, process groups, termios, resize and signals have not been demonstrated. Retain the truthful line backend; no full POSIX job-control claim.

Next bounded investigation: audit the supplied 13.60 libkernel/system-module exports and syscall veneers for an ordinary process-creation and PTY route, including return/error conventions. Establish exact bindings before designing a short-lived child probe. If only privileged scene-loader techniques are available, design their app-owned bootstrap, firmware discovery, failure containment and cleanup separately before live integration. Do not guess offsets or derive PS5 compatibility from FreeBSD VM behavior.

A richer terminal frontend can progress independently, but cannot remove these native execution gates. Owned SSH, package execution and environments must report the same capability limits.

## Diagnostic access

The app writes reports mode `0600`. ps5upload could enumerate them but its file-read operation returned `fs_read_open_failed`. Changing only the two secret-free report files to `0644` allowed reading both reports. This establishes a permission difference between app and helper contexts; it does not identify the helper's UID.

The development client permits this nonrecursive mode change only for `/data/ps5term/status.json` and `/data/ps5term/capabilities.json`. It cannot use that route to change keys, passwords or history. Status publication creates a fresh owner-only file every five seconds. `tools/dev/app_diagnostics.py` makes a report readable immediately before retrieving it and checks live title/PID, build, instance and independent focus. A read racing the next publication can fail and must be retried explicitly.


## Final artifact refresh

A final native build with the updated development evidence tools was installed
and reopened successfully: build `1ac37612a3d40fd72c3b523be43cc56e714b6a453e9f94aa58665209efa1f7c7`,
FSELF digest `282e5307d0b6ae9c8f6020fec2c4c60f69519f44850fff5037ca1bd9a729d69a`.
PID 243, a fresh instance ID and independent foreground focus matched the expected
build. The native baseline again reported UID/EUID/GID/EGID 1, supported pipe/poll,
and successful cleanup. The line shell connected and the UI was observed rendering.
Evidence is `.local/dev/phat-final-evidence.json`; the successful activation record
and backup are under `.local/dev/activation/e9847448a6864fcca62065718c0d17c0/`.

The first refresh attempt exposed a transport leftover:
`sce_sys/param.json.ava-hold`. ps5upload source uses this suffix to defer title
registration until transfer completion. Strict file-list verification rejected it;
rollback restored all backup bytes but also left the duplicate. Both the final and
held metadata were checked against the verified backup hash before removing only
that duplicate. The activation harness now recognizes only the two exact held
metadata names, verifies both copies against the intended manifest, and removes
an identical leftover. A differing held copy is preserved and fails verification.
Seven activation tests cover this and partial-transfer rollback. The next live
activation passed full file-list/readback checks. This harness change postdates the
final artifact's source identity; the application code itself was unchanged.

## Repeatable 13.60 export audit and child reference

`tools/dev/audit_native_exports.py` now reads sectionless x86-64 ELF program
headers, maps dynamic tables through file-backed `PT_LOAD` ranges, and matches
the same NID algorithm used by the native module writer. It rejects malformed
tables and excludes undefined imports. Its JSON always states
`runtime_support_proven:false`. It reads supplied modules in place and emits
metadata, without copying module contents. Five synthetic tests cover known NIDs,
exports versus imports, missing functions and malformed inputs.

The actual supplied modules, rather than SDK stubs, produced this matrix:

| Function | ordinary libkernel | libkernel_sys |
| --- | --- | --- |
| `rfork_thread`, `execve`, `waitpid` | exported | exported |
| `fork`, `vfork`, `sceKernelSpawn` | absent | exported |
| `setsid`, `setpgid` | absent | exported |
| `ioctl`, `kill`, `dup2` | exported | exported |
| `posix_openpt`, `grantpt`, `unlockpt`, `ptsname` | absent | absent |

Input SHA-256: ordinary
`f89a698bf7a0082ec0e361cebf77e40c938196cd13d46016531f71d6ca760912`;
system `60921322b155ba649cfa29ea5e6d358ca0229424b4f73fd5c30ae7f7ce7c304c`.
Private audit output: `.local/dev/exports-1360.json`. Missing exports do not
establish that the corresponding kernel operations are unavailable.

Read-only disassembly confirms that ordinary `rfork_thread` calls syscall 251,
branches on carry to an errno-setting failure path, switches the child to the
supplied stack, calls its callback, and exits using the callback's return value.
The system module's `sceKernelSpawn` implementation also uses `rfork_thread`
internally, with different resource-sharing flags and additional event handling.
Its existence is not a reason to load a second libkernel into the native app.
The system `fork` implementation contains thread/atfork preparation and recovery;
the raw SDK syscall stub does not reproduce those operations.

`tools/dev/rfork_reference_probe.c` initially tested `RFPROC | RFFDG`: copied VM and
descriptor table, a separate aligned child stack, one byte written by a minimal
callback, expected exit status 23, nonblocking parent wait, a deadline, and
explicit descriptor cleanup. It never executes a package or opens a PTY.
It passed on the FreeBSD amd64 lab with data/exit/cleanup true and no timeout.
Evidence: `.local/dev/reference-freebsd-rfork.json`. This remains reference-only.

The flag meanings and callback contract were cross-checked against the FreeBSD
Project's [rfork(2) source](https://github.com/freebsd/freebsd-src/blob/releng/11.0/lib/libc/sys/rfork.2)
and [rfork_thread(3) source](https://github.com/freebsd/freebsd-src/blob/releng/11.0/lib/libc/gen/rfork_thread.3).
These describe upstream FreeBSD, not Sony's kernel behavior.

The app experiment uses a separate opt-in request and report, never
run at startup, and attempt only this short-lived child. The generic probe runner
now supports an optional nonblocking `drained` callback after initiating cleanup
exactly once. A timeout or cancellation retains the active operation until that
callback confirms completion; `done()` stays false and a new `start()` is rejected
while draining. Regression cases cover timeout, cancellation, repeated cancel,
successful-step draining and preserving the original operation during cleanup.
The adapter owns retained resources and its context must remain alive until the
runner is done; a destructor cannot synchronously finish asynchronous reaping.

The native child adapter now implements this contract: retain
ownership/handles until reaped, prohibit a second start while draining, and avoid
signaling a PID after `ECHILD`. Do not report a
successful process probe solely because creation returned a positive PID.
Only a subsequent, separately bounded exec test can establish package launch;
this experiment cannot pass Gate A or Gate B on its own.

## Bounded native child experiments

`src/platform/native_child.*` attempts only a tiny callback and expected exit 23.
It retains its 64 KiB stack and descriptors through asynchronous reaping, sends
at most one stop signal, and refuses to signal after losing ownership. An
unresolved reap remains draining and prevents another experiment. The separately
compiled `PS5TERM_CHILD_RESEARCH` service requires the exact `child-v1` request;
ordinary startup and the baseline request do not create a child. Reports preserve
failures, incomplete cleanup, build identity and measured app PID.

Three native app runs on the phat returned the following actual results:

| Child mode | App PID | Child PID | Result | Completed / cleanup |
| --- | --- | --- | --- | --- |
| `RFPROC | RFFDG`, copied VM / copied FD table | 790 | -1 | `EPERM` (1) | true / true |
| `RFPROC | RFFDG | RFMEM`, shared VM / copied FD table | 817 | -1 | `EPERM` (1) | true / true |
| `RFPROC | RFMEM | RFCFDG`, shared VM / empty FD table | 834 | -1 | `EPERM` (1) | true / true |

No child was created in any of these runs. These are permission failures for these exact
calls from the measured UID 1 native app, not proof that every native launch API
is unavailable. The app remained running. Identity and native pipe/poll succeeded;
process-group, PTY, resize and descriptor interoperability remain unknown.
Gate A and Gate B remain unpassed. A requested SSH setting is not SSH health.

All three installations used verified backups/readback and observed normal home-screen
launches. Private evidence is in `.local/dev/phat-child-copied-evidence.json` and
`.local/dev/phat-child-shared-evidence.json`. Shared-VM build identity:
`0e58dae64ac1ee1087bb06453c3de8c6c56d7d8c25dd96fcf30dadc4d7e61036`;
FSELF digest `59d15d2310e6bd8506710d3766474752ef2ef41bac7d96a5cd4a7a853814f630`.

The third variant followed the exact `RFPROC | RFMEM | RFCFDG`
resource mode observed in both the supplied system-module spawn implementation
and the existing elfldr source: an empty child FD table. It uses a shared memory
marker instead of an inherited pipe. Private evidence is
`.local/dev/phat-child-empty-evidence.json`, build
`7764aa351794e9443a03682d9ac5022831c479a94cc391699b63b104ecb1927e`.
It does not load system libkernel, exec a
system process, change credentials, or touch kernel state. Its FreeBSD reference
passed marker/exit/cleanup with no timeout (PID 1800); that is not PS5 evidence.

After the portable VT foundation and converter fix, the full Linux suite passes
41 Python tests, 3 renderer tests and 15 terminal suites. All 15 macOS terminal
suites pass. Native candidates pass FSELF validation;
these checks alone do not establish standalone launch or a functional shell.


## Narrowing the EPERM investigation

Read-only inspection of the supplied ordinary libkernel also confirms exported
`getrlimit`, `getrusage`, `sceKernelGetProcessType`, `sceKernelGetProcessName`,
`sysctl`, `sysctlbyname` and `issetugid`. Exports alone establish neither ABI nor
runtime permission. `getrlimit` is a carry-to-errno syscall-194 wrapper, matching
the SDK declaration. The process-type export calls another wrapper and translates
native errors; its exact callable signature has not yet been sourced, so it was
not called speculatively. No new console experiment or process mutation occurred.

In upstream FreeBSD 11's [fork implementation](https://github.com/freebsd/freebsd-src/blob/releng/11.0/sys/kern/kern_fork.c),
process-table and per-user process-limit exhaustion produce EAGAIN. Invalid rfork
flags produce EINVAL. There is no explicit EPERM return in that source file.
This comparison makes a simple FreeBSD RLIMIT_NPROC exhaustion explanation less
compelling; it does not identify Sony's actual refusal branch. Sony-specific
syscall policy, process context and launch restrictions remain hypotheses.
Increasing limits or changing identity would not be a justified next step from
this evidence. Next, identify the actual refusal path in an existing kernel
capture or source a documented native launch API before another process test.
The inspected elfldr bootstrap changes capabilities/authentication/root context
and injects a system service; transplanting it would change the design's safety
and lifetime assumptions, so it was not silently embedded as a spawn fix.

### Archived refusal branch and a package-only experiment

Read-only analysis of the existing private 13.60 archive found the native
syscall-251 entry and followed its handler's first permission call. That helper
tests bit 62 of the first capability word; failure returns EPERM before flag
validation and process creation. The archive is from the Pro, so this is a
specific explanation to test on the phat, not proof of its live refusal path.
Capture provenance and disassembly remain private; no dump bytes or addresses
are compiled into the application.

The public [kstuff executable auth metadata handler](https://github.com/EchoStretch/kstuff-lite/blob/48cd2c4ce62aa9ca0c2cc014c1e512f276ae7c17/ps5-kstuff/uelf/fself.c)
accepts an optional 136-byte record in an executable container. Its default
native executable record lacks that bit. The next bounded experiment uses
`APP_AUTH_PROFILE=rfork-research` plus `PS5TERM_CHILD_RESEARCH` to preserve the
public default authority and all other record fields, adding only bit 62.
The build generates a distinct `PS5TERM_RFORK_AUTH_RESEARCH` identity marker.
Normal builds have no custom record. This requests a capability during ordinary
app loading; it adds no runtime credential writes, kernel patch, system-service
injection, root identity or companion payload. Borrowed format data is credited
in NOTICE.md. This profile is experimental and is not a release default.

Before hardware activation, inspect the actual container at the loader's record
offset and verify the complete record against the intended one-bit change.
Repeat only the copied-VM/copied-FD-table child probe first. Success would prove
that bounded child experiment, not executable loading, PTYs, SSH independence,
cross-firmware support or completion of Gate A.

The first package test did **not** reach the probe: the added-bit package
(`ff1bc6b1…`, build `917df88c…`) produced CE-108255-1 on normal launch.
The verified previous build was restored and reopened (PID 1810). Two controls
using the exact same research executable then launched normally: no custom auth
record (PID 1816, FSELF `6fedb0ae…`) and a custom record containing the unchanged
public baseline (PID 1821, FSELF `89150996…`). This isolates the failure to the
added capability under this launch context, not merely custom-record presence
or the new executable. The underlying startup fault is still unknown. No child
request was sent to the failed instance; do not report its result as rfork EPERM
or claim the capability route works. Do not enable the added-bit profile in a
release or repeat it without a specific startup-fault hypothesis. The next
independent work is terminal rendering/session integration.


## Owned local interpreter and native directories

The default local connection now executes seven bounded built-in commands inside
PS5Term: `help`, `pwd`, `cd`, `echo`, `id`, `ls`, `cat`. It uses per-session logical
working directories and does not change global cwd or standard descriptors.
This removes the local console's external-loader dependency without claiming
process execution, a PTY, pipelines or a complete shell. SSH is still separate
legacy code and remains outside this acceptance result.

Controller entry of `id` and `help` was observed on phat. Identity again reported
UID/EUID/GID/EGID 1. Native keyboard entry/Enter, `cd /data`, `cd /app0`, and
`cat /app0/assets/build-info.json` were observed. The missing
`/app0/sce_sys/param.json` returned ENOENT; no claim is made that this path exists
in the title's app0 mount.

Imported libSceLibcInternal directory APIs initially returned EPERM. Replacing
them with the public libkernel `getdirentries` wrapper and a validated bytewise
SDK/FreeBSD11 record decoder reached EINVAL with a 4 KiB buffer. A comparison
with [Kodi's native PS5 directory notes](https://github.com/Phoenixx1202/kodi-ps5/blob/main/shims/native-app/libc_posix.c)
identified the 64 KiB app0 directory-block requirement. A host test reproduces
that boundary failure, verifies the native adapter's record consumption and real
fd cleanup, then passes with the fixed 64 KiB buffer. PS5Term does not copy the
Kodi implementation or grow buffers without a bound.

Final native build `49d88046a55f71c9fbbc3860e350db6f3e387b98d57198d0d933d2aa61224141`,
FSELF digest `7ce0a1c264a53d1ad1a31467bcb1ec90f02e90340c7bb19502f1da29f8e0a3af`,
was backed up, installed and reopened via observed home-screen Cross. Independent
readiness matched PID 1950, instance `79e-855a7e9c1` and foreground focus. The
normal public auth profile was used, with `PS5TERM_CAPABILITY_RESEARCH` compiled.
Both `ls /app0` and `ls /app0/assets` visibly listed the expected app files and
returned to the prompt. Private evidence is `builtin-directory64-activation.json`
and `builtin-directory64-readiness.json` under `.local/dev/`.

This demonstrates the bounded local fallback on one firmware and app filesystem.
It does not establish arbitrary filesystem access, other firmware compatibility,
native child/PTY support, authenticated SSH health or subsystem support.
