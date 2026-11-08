# Linux subsystem (experimental)

The default development app bundles a small Linux environment. It runs inside PS5Term:
no ELF loader, additional payload or separate installation is needed. Native PS5
commands remain the startup environment; Linux starts only when selected.

The quickest way in is SSH from your computer. Settings shows the password, and the
login starts Linux if it is not running:

```sh
ssh -p 2222 linux@<PS5-IP>
ssh -p 2222 linux@<PS5-IP> 'cat > /mnt/data/notes.txt' < notes.txt
```

Each login gets its own shell and terminal size, up to four at once, separate from the
console's screen. Stopping Linux ends these sessions with a message.

On the console, open Settings with F6, click Settings, or press Options. Choose
**Start Linux**:

- Keyboard: Tab/Shift+Tab or Up/Down to focus it, then Enter/Space.
- Mouse: click Start Linux.
- Controller: D-pad Up/Down to focus it, then Cross.

Close Settings with Esc, Back, or Circle to see the guest console. Enter commands
using a keyboard or Triangle's on-screen keyboard and R2. Reopen Settings and
choose **Stop Linux / return to PS5** to stop the guest and release its memory.
Reconnect restarts a stopped environment; a running Linux asks you to click twice,
because it is restarted. The bundled root disk is temporary: **changes outside
`/mnt/data` are lost when stopped or the app closes**.

The default `PS5TERM_LINUX_PERSISTENCE` build adds a separate 16 MiB ext2 volume
at `/mnt/data`, backed by `/data/ps5term/linux/data.ext2`. It is initialized only
when absent; updates never replace an existing volume. The guest checks it with
`e2fsck -p` before mounting; serious errors leave it unmounted for recovery.
Use `mount` to confirm `/dev/vdb` is mounted before saving files. Stop Linux flushes
and remounts the disks read-only before halting, so recent writes survive. Closing
the app from the PS5 home screen gives no such chance: run `sync` first, though the
guest also writes changed data out within about two seconds.
A filesystem check cannot guarantee recovery from every interrupted write.
Back up the native disk file only while Linux is stopped.

### Larger data disks

Settings → Expand Linux storage prepares a 512 MiB backing file, retaining a
rollback backup. Start Linux next to check and expand the filesystem. The loader
accepts sector-aligned ext2 files from the 16 MiB seed size through **1 GiB** without
loading them into RAM. The default seed remains 16 MiB. This expansion and Alpine
installation were exercised on the phat console with firmware 13.60.
Older November 4 app builds only accept 16 MiB; retain the original backup before
downgrading or moving a disk to an older build.

For an offline copy on macOS or Linux, install e2fsprogs (`brew install e2fsprogs`
on macOS), stop Linux in Settings, and download
`/data/ps5term/linux/data.ext2` using the native `ps5term` SFTP login or ps5upload.
Keep that download as a backup. Then run:

```sh
python3 tools/dev/grow_linux_disk.py --source data.ext2 \
  --output data-512m.ext2 --size-mib 512
```

The tool locks the local source, checks its geometry, copies it, checks and expands
the copy, then checks it again. It refuses to overwrite an existing output and
leaves the source unchanged. It cannot detect a guest still writing on the console:
**stop Linux before downloading or replacing its disk**. After deploying a compatible
app, upload the verified output under a temporary name in the same native directory,
verify the transfer, and rename it to `data.ext2` while Linux remains stopped. Retain
the original offline until a restart and file checks succeed. Never resize a mounted
disk. Extra space does not add Alpine or a package manager to the Buildroot guest.

Your shell history is kept in `/mnt/data/.ash_history`. Programs in `/mnt/data/bin` are
on the `PATH`, and `/mnt/data/profile` runs at login for your own settings.

The guest includes Linux 6.12.112, BusyBox 1.37, musl, ncurses/256-color terminfo,
less, curl with mbedTLS and CA certificates, nano, tmux, htop, jq and tree. It has a
POSIX shell, scripts, pipes, guest processes and PTYs. The boot system has no package manager; the optional persistent Alpine environment
below supplies `apk`. The boot system is a
minimal RISC-V Buildroot system, interpreted in software, with 128 MiB guest RAM
and a 16 MiB root disk. It is not an amd64 Ubuntu installation. The tracked engine
heap is capped at 160 MiB, in addition to bundled-image buffers, terminal buffers
and native app overhead. One owned worker runs one guest; stopping joins it before
freeing its borrowed images. The native memory adapter uses page-rounded,
read/write flexible mappings, without executable memory or privilege changes.

## Network

`eth0` gets `10.0.2.15` by DHCP at boot, through an in-app NAT built on libslirp. DNS
and outbound IPv4 TCP and UDP work, including HTTPS, with the time and certificates set
up for you. Nothing can connect in to the guest, and it cannot reach the app's own
loopback services. It can reach your LAN and the console's LAN address like any other
device. There is no IPv6 yet. `ping` only answers for the gateway `10.0.2.2`, so test
connectivity with `curl -I https://example.com`.

DNS goes to 1.1.1.1. If your network blocks that, write a resolver's IPv4 address to
`/data/ps5term/linux/resolver` and restart Linux. If the network backend fails, Settings
says so and Linux keeps running offline.

## PS5 files

`/mnt/ps5` shows the PS5's own files, using an in-app virtio-9p device with no network
service or extra payload. Settings has a **PS5 files in /mnt/ps5** control with two choices:

- **`/data`** is the default.
- **`/`** is everything this app can see, which is its sandbox rather than the whole PS5. On a
  phat at firmware 13.60 that was `app0`, `data`, `dev`, `download0`, `mnt/usb0`, `mnt/ext0`,
  `preinst` and `system_tmp`, with no `/system`, `/user` or `/system_data`. USB drives were
  writable; `mnt/ext0` was readable but refused writes. Linux root can change or delete whatever
  the app can reach there.

The choice is saved in `/data/ps5term/linux/host-mount` and applies the next time Linux starts.
A damaged file means `/data`. If the chosen folder cannot be opened, Linux still starts without
`/mnt/ps5`, and the status line says so.

```sh
ls /mnt/ps5
cp /mnt/ps5/notes.txt /mnt/data/
```

Subfolders, large files, creating, overwriting, appending, deleting, renaming, `chmod` and
timestamps work, with the same permissions the app has; Linux root does not grant anything
beyond that. The data volume at `/mnt/data` remains a separate emulated disk.

What it does not do:

- **Two folders stay hidden in both modes:** `ps5term/ssh` and `ps5term/linux`, wherever they
  appear, such as `/user/data/ps5term/ssh`. They hold your SSH password and host key, and the disk
  Linux is running from. Programs in Linux can reach the Internet, so they cannot read these.
  Any folder named `ps5term` cannot be renamed, deleted or replaced from Linux either, since
  that would move them out of view. Change them with the `ps5term` login instead.
- **`chmod` never follows a symlink,** so it cannot change a file outside the shared folder.
- **Only regular files open.** Devices and FIFOs are listed but refuse to open, because a native
  device could stall the emulator.
- **No symlinks, hard links or device nodes can be created,** and ownership stays root. Existing
  native symlinks are listed, but their targets cannot be read or followed on the console.
- **Programs cannot run from `/mnt/ps5`,** which is mounted `noexec`. Copy them to `/mnt/data`.
- **It is slow for large files,** because every request goes through the interpreter. The same is
  true of `find` over a large tree.
- **No file locks.** Programs that insist on `flock` or `fcntl` locks there get an error.
- **`df` shows 0 for `/mnt/ps5`.** The app's sandbox refuses the size query at firmware 13.60.
  Writing past the real free space still fails with a normal disk-full error.

Changes made natively by other PS5 software appear immediately, as nothing is cached in Linux.
Files written through Linux are flushed when they are closed. This does not make `/mnt/ps5` a
safe place to write while a game or system service is using the same files. Up to 64 files and 16
directory listings can be open at once.

## Evidence and limits

On phat firmware 13.60, an installed native app booted this guest, accepted
controller/on-screen-keyboard commands, reported `6.12.112-ps5term`, ran a shell
pipeline, stopped to the native console and booted again. Native SSH continued
to authenticate with the existing identity. The updated guest also initialized,
checked and mounted the data volume, and a file written just before Stop Linux
survived with a clean filesystem. On the same console, `ssh linux@` started the guest,
ran two independent PTYs, resized live, fetched over HTTPS, handled Ctrl+C and exit
status and carried binary data intact through an exec. A 9.2 MB HTTP download through
the NAT ran at about 8 MB/s. No Pro console was used. This does
not establish compatibility with every firmware or physical keyboard/mouse.
Host tests additionally cover shell/fork/wait, PTYs, resize, terminfo, repeated
boot, unchanged base image, allocation failures, backpressure and stop/join.

The subsystem remains experimental. Builds omitting `PS5TERM_LINUX_SUBSYSTEM`
show it as unavailable and omit the image assets. Enable the build profile with:

```sh
APP_DEFINITIONS='PS5TERM_OWNED_SSH PS5TERM_OWNED_SFTP PS5TERM_LINUX_SUBSYSTEM PS5TERM_LINUX_PERSISTENCE PS5TERM_LINUX_SHARED_FOLDER PS5TERM_LINUX_NETWORK' bash tools/build.sh Folder
```

Omit `PS5TERM_LINUX_PERSISTENCE` for a disposable-root-only build. Native storage
uses a fixed app-private path: Sony's app context rejects `openat`. It checks the
opened parent identity and refuses final-component symlinks. This is not
confinement against another privileged process renaming directories. Native
`/data` reports zero link counts, so zero is accepted as unavailable metadata;
host tests require exactly one link. Back up data before experimenting with it.

Pinned image identities are in `tooling/subsystem/guest-images.json`. The build
recipe, generated configs, TinyEMU patches and source/license collection are in
`tooling/subsystem/guest`, `tools/build-modern-guest.sh` and
[the research record](research/2026-11-01-embedded-linux-results.md). The old 2018
sample image is not shipped. Local corresponding guest source is staged in
`build/modern-guest-source-delivery`; its companion archive must accompany any
public image release. This local development package has not been published.

## Linux environments and packages

Settings → Linux environments lists Base (the guest's own BusyBox system, always there),
Alpine 3.23 and Ubuntu 24.04 LTS. Alpine and Ubuntu are official riscv64 root filesystems,
pinned by URL and SHA-256 in `ps5term-env`, unpacked into `/mnt/data/<name>` and entered
with `chroot`; the kernel stays PS5Term's. Nothing unverified is unpacked.

- **Open** (or **Install and open**) starts Linux if needed and opens a new tab running
  `ps5term-env open <name>`: a first open downloads and unpacks it there, with progress.
  `exit` ends that tab's shell; Reconnect opens it again.
- **Make default** chooses where new Linux tabs and `linux@` SSH logins start. Commands sent
  over SSH (`ssh -p 2222 linux@ps5 'cmd'`) still run in Base, as scripts expect. If the
  default cannot start, the tab stays in Base with a one-line reason.
- **Remove...** asks again, then deletes only that environment's folder. It refuses while a
  shell or SSH session is inside it.
- **Grow to 1 / 2 / 4 GiB** (Linux stopped) first copies the data disk to
  `/data/ps5term/linux/data.ext2.backup` in a worker (a later growth replaces that backup),
  then extends the disk with zeroed space. Keep the app open until it finishes; the PS5
  needs room for the backup and the larger disk. At the next Linux boot the guest checks and grows the ext2 filesystem; a
  failed check leaves it unmounted. The loader accepts disks up to 4 GiB.

Alpine needs at least 64 MiB free and is about 10 MiB. Ubuntu needs 384 MiB free: its base
is 70 MiB, and `apt update` adds about 260 MiB of package lists, so grow the disk to 1 GiB
first. On the phat (13.60) the Ubuntu download took about a minute, unpacking a minute,
`apt update` six minutes and `apt install nano curl` six minutes; memory use stayed under
40 MiB. Everything is emulated, so `apt` is slow; `apk` is much quicker.

The same commands work in any Linux shell or `linux@` SSH session:

```sh
ps5term-env list              # every environment, its state and size
ps5term-env open ubuntu       # install if needed, then enter
ps5term-env run alpine apk add bash
ps5term-env default ubuntu    # new shells start here; "base" goes back
ps5term-env remove alpine
```

`ps5term-alpine` keeps working as the older name for the Alpine commands. Installed
packages, `/root`, and changes within `/mnt/data/<name>` persist; the Buildroot boot
filesystem stays temporary. Each environment's `/mnt/ps5` shows the same selected PS5
files as Base. `/proc`, `/sys` and `/dev` are shared with the Linux guest; a chroot uses
the guest's kernel and is not an isolation boundary. It is not a native PS5 jail, hardware
VM, or x86 Linux environment. Ubuntu 25.10 and later need RISC-V vector support (RVA23),
which the emulator does not have, so 24.04 LTS is the Ubuntu offered.
RISC-V packages run through interpretation, so substantial workloads can be slow;
the guest still has 128 MiB RAM. Never install FreeBSD or x86 packages into it.

Host sanitizer tests cover expansion, installation, HTTPS apk installation, and
restart persistence; `tests/guest/test-ps5term-env.sh` checks `ps5term-env` on riscv64
Linux (verified install, run, default, in-use refusal, remove, space check). The phat 13.60 also completed Settings-driven expansion and
Alpine installation, plus apk installation of Bash over SSH and package/home-file persistence across
Stop/Start. Other firmware and
console models remain untested.
