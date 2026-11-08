# Terminal transport and subsystem integration

Execute inline with the executing-plans skill. User delegated decisions and
explicitly opted out of questions. Preserve other-agent work; no commits/push.

Design: native commands remain available without kernel/credential mutation.
Native PTY experiments require an explicit build flag on PS5. Linux is an
optional owned worker, using the same VT model and bounded transport as native
sessions. Prefer pinned software emulation over assuming jail/Linux ABI or
hardware virtualization support. Never ship the old 2018 research guest.

- [x] Gate PS5 native PTY attempts; remove automatic privilege escalation from
  the launcher. Verify a simulated PS5 default factory refuses launch without
  touching the privilege function. Preserve host PTY reference tests.
- [x] Reproduce and fix pipeline-plus-redirection and buffered pipeline overflow;
  verify combined pipes, append, failure status and descriptor cleanup.
- [x] Add bounded retained terminal input/output transport with deterministic
  short-write/backpressure/reply/final-drain tests. Route Connection through it.
- [x] Route frontend keys/paste/replies and resize through the VT model without
  changing controller/pointer ownership conventions.
- [x] Make Linux build selection explicit before compilation and identity;
  prepare pinned sources independently of old build directories. Test profile
  preparation and verify native compile/link separately from physical runtime.
- [x] Prepare modern guest and source/license/terminfo delivery; verify host
  boot and app lifecycle, then phat-only opt-in hardware acceptance through
  ps5upload backup/install/readback and observed Home Cross.

Checks: tests first for behavior; focused host suites before full terminal suite;
bounded compiler parallelism; no Pro, reboot, raw payload or automatic privilege
experiments. A successful host shell never establishes native PS5 capability.
No release claim until artifacts match sources and physical evidence.


2026-11-02 integration: default native and optional Linux profiles both compile,
link and validate their signed containers. New retained transport and app VT
regressions pass with the terminal suite on macOS and Linux. Linux reference
portability fixes cover sysctl/gethostname, direct algorithm include and PTY
header/feature declarations. Python tooling: 61 tests pass. Native dependency
caches now follow compiler target; default build owns SSH/SFTP and omits legacy
payload assets. Physical acceptance and the modern guest remain separate gates.

Modern guest candidate: Buildroot 2025.02.19 LTS (official release page), source
archive SHA-256 e5e58bff1da91f022f60cefdfe280140c08023cd0aeec1e5f6f9da0ea866d69c,
observed from official HTTPS; PGP signature not verified. Cached archive only.
Its default Linux is 6.12.112 and OpenSBI is 1.6. Do not infer TinyEMU compatibility:
prove ISA/SBI/console/block support with a new minimal guest before UI enablement.
Generic data-only tar extraction refused an absolute rootfs-skeleton symlink;
the partial extraction was removed. Build in a disposable Linux volume with an
explicit archive extraction policy; never reuse that partial tree.


Physical acceptance round 1: default build 94a6bf3a launched on phat13.60,
PID4552 / instance11c8-2462e85446, foreground independently verified. The first
readiness sample was too early; a later sample passed without another launch.
OpenSSH authenticated using the existing password, UID/EUID/GID/EGID1. Actual
pipe-to-file, append and readback passed. Local Paramiko's Python process could
not route directly to the console (errno65), while /usr/bin/ssh worked; no network
permissions changed. OpenSSH negotiated mlkem768x25519-sha256 with the existing
host key and no PQ warning. Two runtime gaps reproduced: rm's lstat returnsEPERM,
and unset APP_DEFINITIONS selected SFTP in the app but not its library builder.
The latter now propagates the resolved profile; rm/stat use a no-follow ordinary
file/directory descriptor fallback matching the already-tested SFTP policy.
Rebuild/redeployment and hardware green checks are required below.


Final physical green: build f18466904b9bd6488a4c373214e81da9c513c9f9ff9dd540ab31ae65be41cbce,
PID4553, new instance and foreground verified. Owned SSH authentication succeeds;
stat reports the real 27-byte regular file, rm succeeds and subsequent cat returns
ENOENT. The previous test file is removed. Actual OpenSSH SFTP authenticates and
lists /app0. Controller Triangle/Circle opens/closes the keyboard; Options/Circle
opens/closes Settings; password stays hidden and SSH stays enabled. App remains
foreground. Default archive 638027 bytes, inputs match identity and ZIP CRC passes.
Private activation/readiness/artifact/hardware records: .local/dev/integration-20261102.
The optional Linux profile compile/link passed earlier in this turn; it was never
deployed. Modern guest and Linux runtime remain unfinished, not release claims.

Modern guest implementation underway: `tooling/subsystem/guest` is an owned
BR2_EXTERNAL tree, `tools/build-modern-guest.sh` a Linux-only bounded resumable
recipe with source/license collection, and `tools/dev/modern_guest.py` a pinned
source extractor with explicit link policy. Header/size staging checks are in
`tools/dev/guest_images.py`. Five added tooling tests pass (66 total). Actual
modern guest acceptance tests kernel version, shell, fork/wait, PTY/resize,
terminfo, repeat boot and immutable base disk; it is not yet a native proof.
The first internal compiler build runs in Linux scratch. Final external-tree
normalization and BusyBox PTY recording/getty defaults require a reconfigure
before publishing the candidate. Keep the working native app installed meanwhile.

Session ownership decision: one Linux guest per application. Begin with a local
console and explicit native SSH behavior; do not create an interpreter per SSH
client. A later guest agent can provide independent guest PTYs over a bounded
multiplexed transport. Avoid mixing remote input into the visible local console
or presenting guest root as native PS5 root. Guest disk writes initially remain
ephemeral; persistence and larger complete distributions require a bounded
file-backed disk design rather than copying an entire larger disk into RAM.

Modern guest host milestone verified: Linux6.12.112/OpenSBI1.6/BusyBox1.37.0,
musl1.2.6, ncurses256-color terminfo and 16MiB ext2 boot through the owned
interpreter, not QEMU. macOSarm64 ASan/UBSan and Linuxaarch64 ASan/UBSan/LSan
acceptance pass: version, functions/pipeline, fork/wait status, real PTY, live
resize, terminfo, two boots, unchanged base disk. Existing runtime/worker OOM,
flow-control, cancellation and lifecycle suites pass with patched core on both
hosts. Updated PS5 SDK object compilation, relocatable and no-undefined shared
link checks pass; this is NOT physical guest proof. Python tooling66pass.

Modern firmware required HTIF MMIO addresses in FDT, read-only unspecified-zero
mvendorid/marchid/mimpid, and TIME access-enable preservation for firmware trap
emulation. Early SBI logging exposed the timer trap. Disable HVC_RISCV_SBI so
virtio gets hvc0; otherwise the visible shell never receives app input. All fixes
are in the owned core patch/config; regression and modern acceptance pass.

Current candidate: build/modern-guest-virtio-candidate; evidence under
.local/dev/modern-guest-20261102, including red failures and final green hashes.
Source/license bundle: build/modern-guest-source-delivery,192+ files, ~303MiB.
Includes upstream legal-info sources/licenses, exact Buildroot archive, generated
configs, owned external tree and patch, acceptance evidence and SHA256 manifest.
Buildroot warns its own source is omitted by legal-info; the archive is supplied
alongside it. PGP verification, clean second build/repro comparison and full
wrapper end-to-end run still unverified. The existing manual build was normalized
with final BR2_EXTERNAL config and explicit busybox/linux reconfiguration.

Last checkbox remains pending: no Settings environment selector, no native
modern-guest packaging/boot proof, no SSH-to-guest PTY bridge. Linux app startup
remains disabled. Next integrate an explicit local environment switch, validate
images before allocating, release session/image resources on failure and stop,
then build optional app with matching images and source delivery and perform
phat-only gated ps5upload acceptance. Keep native SSH behavior separate.

2026-11-03 native subsystem milestone: modern images are bundled in the default
development build, Linux off at startup. Explicit Settings Start/Stop works with
keyboard focus, mouse target and controller focus/Cross. Validated image loading
rejects links/nonregular/malformed/oversized files and joins before releasing
borrowed images. Failed starts unwind; stopping releases all image buffers.
The POSIX mmap import caused SYSTEM_ILLEGAL_FUNCTION_CALL in this native app.
A page-rounded sceKernelMapNamedFlexibleMemory/sceKernelMunmap adapter fixes it,
with host mock tests for flags, rounding, failure and overflow. No privilege
changes or loader calls. Native phat13.60 boots Linux6.12.112, accepts controller
uname/pipeline/stty commands, stops/boots again; guest id is0 while nativeSSH id
is1. Final build28d1a0df PID4578 boot/id/stop/native-console and SSH acceptance
pass, same existing credentials. LNM is explicit per environment, fixing native
LF staircase after guest reset; regression reproduced red then passed green.
Guest networking, persistent disk, PS5 sharing and independent SSH guest PTYs
remain unimplemented. Public release/source-delivery review and physical USB
keyboard/mouse acceptance remain separate gates. Evidence: private
.local/dev/linux-settings-final-20261103. Pro untouched.
