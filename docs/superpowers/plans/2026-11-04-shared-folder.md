# Bounded PS5 exchange folder implementation plan

**Goal:** Read and export regular files between Linux `/mnt/ps5` and native
`/data/ps5term/shared`, entirely inside PS5Term.

Superseded 2026-11-04: `/mnt/ps5` is now a path-based passthrough of the PS5's `/data`, or `/` by
user choice. See `docs/SUBSYSTEM.md`. What follows records the first flat design.

**Architecture:** Optional virtio-9p device, small C adapter, fixed-capacity C++
file owner. One flat directory, 64 files, 4 MiB/file and 32 MiB aggregate. ASCII
single-component names up to 63 bytes. Existing files opened once and held until
worker join; no guest-selected host paths. Guest supports listing, reading,
creating, overwriting, truncating, deleting and renaming files. A removed file
keeps its slot and descriptor until the last 9p fid closes, so a new file is never
reachable through a stale fid. No directories, links or device nodes. Host changes to names require Linux restart; contents
of an existing descriptor remain live. Use 9P2000.L, cache=none, nodev,nosuid,noexec.

**Constraints:** PS5 app rejects openat. Host uses descriptor-relative opens;
native uses validated fixed-directory paths, nofollow, parent identity checks.
This is a cooperative app-private exchange, NOT confinement against another
privileged native process replacing parent directories. No full PS5 root mount.
Guest is trusted, existing emulator is not a malicious-guest sandbox. Keep this
profile optional until guest/native build and phat-only hardware acceptance pass.

- [x] Test bounded file owner: traversal, symlinks/specials, descriptor lifetime,
  caps, existing-file preservation, create/exclusive/truncate/write/read.
- [x] Implement C 9p bridge, root/fid limits, negative Linux error mapping,
  exact directory cookies, unsupported operation responses. Actual guest tests.
- [x] Build Linux 9p kernel/overlay candidate with private staged pins. Verify
  normal guest regression and share absence compatibility.
- [x] Wire optional loader ownership, Settings guidance and packaging capability.
  Build native, phat-only guarded update and controller read/write acceptance.
- [x] Refresh source/license delivery and evidence; cleanup only generated copies.

Next independent milestone: maintained userspace NAT plus SceNet adapter. Do not
claim networking is enabled by a working shared folder.
