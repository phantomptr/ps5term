# Subsystem storage, network and exchange implementation plan

> Execute inline, preserve the existing untracked worktree. Phat only.

**Goal:** Give the embedded guest durable files, outbound networking and an
explicit native file-exchange folder, without a separate payload.

**Architecture:** Retain the immutable bundled root image and single worker.
Attach a separately owned, bounded file-backed data disk. Never copy PS5 root
into the guest. Keep native file exchange rooted at `/data/ps5term/shared`.
Network requires a separately reviewed userspace TCP/IP backend and guest virtio
network support; do not enable the old bundled SLiRP merely because it compiles.

**Tech stack:** Existing C interpreter/worker, C++ platform ownership, Linux
virtio block/network, Buildroot musl guest; no native fork, privilege changes,
POSIX mmap or additional loaders.

## Storage design

Use a fixed-size 16 MiB ext2 data volume, exposed as `/dev/vdb` and eventually
mounted at `/mnt/data`. The root disk remains disposable. Create the volume from
a separately pinned seed, preserving existing data on every start/update. The
platform owns the opened fd until worker join. Engine callbacks do range-checked
sector reads/writes on the worker; no UI-thread filesystem work during a batch.
Writes finish only after fsync, because the current virtio disk advertises no
writeback cache or flush feature. Refuse symlinks, nonregular or wrong-size files,
concurrent writers and failed storage; never silently fall back to a temporary
volume when persistent mode was requested. Runtime error is a guest IO error.

A crash/power loss can still interrupt guest filesystem changes; write-through
is not a filesystem transaction. Add guest boot-time fsck before automatic mount,
and keep the volume unmounted on failed checks. Never format/repair an existing
volume automatically from the native application. Do not expose a persistent
root filesystem or Alpine/package installation until reboot/recovery works.

## Tasks / gates

- [x] Add optional second block-device callbacks to engine images; maintain
      legacy single-disk behavior. Verify with actual Linux mounting vdb.
- [x] Implement bounded file-disk ownership, checked initial seeding, exact I/O,
      exclusive lock, fsync and lifecycle tests (including malformed files).
- [x] Package pinned seed and guest fsck/mount support, connect platform lifetime,
      expose storage status to keyboard/mouse/controller Settings.
- [x] Prove guest writes survive stop/start, base image stays unchanged, errors
      remain visible; SDK compile then phat-only guarded deployment/acceptance.
- [x] Implement rooted exchange backend. The user later chose a passthrough of `/data`, with
      the whole console as an explicit opt-in; see `docs/SUBSYSTEM.md`.
- [x] Select maintained userspace NAT with PS5 socket adapter, pinned source and
      license; test guest DNS/TCP/UDP/HTTP plus stop/resource cleanup. Add guest
      kernel NIC/IP support and honest Settings status before enabling.

No shipping metadata should claim network/share/persistence proof from host
unit tests. Keep installed known-good app unchanged until the complete selected
milestone passes host and native build checks. No Pro access.

Native storage findings (phat 13.60): direct SDK openat aborts the app;
exported _openat returns EPERM. Use fixed private-path open after parent identity
check, imported flock, and accept unavailable native zero-link metadata. Host
hardlink rejection remains strict. This is not hostile-process isolation.
Actual guest initialized/fsck/mount/write/sync and Stop/Start retention pass;
full app update/restart retention also passed (controller cat returned saved). Runtime allocator regression
also reproduced mapped-buffer free/realloc mismatch; matching unmap fixes it
under ASAN/UBSAN, lifecycle/OOM and file-backed guest tests.
