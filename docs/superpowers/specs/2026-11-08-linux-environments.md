# Linux environments: one place to run and manage Linux

Status: implemented and verified on the phat (13.60), 2026-11-08. Ubuntu 24.04 prototype:
download 68 s, unpack 65 s, base 70 MB, `apt update` 3-6 min and about 260 MB of lists, memory
under 40 MB. As built: Open/Install runs `ps5term-env open <name>` in a new tab (progress shows
there); the list is read quickly first and sizes (`du -x`) afterwards; growing copies the disk
once to the backup and then extends it in place (512 MiB to 1 GiB in under 90 s).

## Why

Linux today is a few separate controls in Settings (Start/Stop, PS5 files, Expand, Open / install
Alpine) and a shell command. Users asked for other distributions (Ubuntu first) and found it
unclear that `apk` only works after opening Alpine. A dedicated **Linux** screen makes the
environments visible, installable and removable, and keeps Settings short.

## Words

- **Linux**: the PS5Term guest (RISC-V Linux 6.12 in the built-in emulator). It always boots the
  small **Base** system (BusyBox, curl, nano, tmux, htop).
- **Environment**: a userland that runs inside Linux with `chroot`, on the persistent data disk:
  Alpine, Ubuntu, later Debian. It has its own package manager; the kernel stays PS5Term's.

## The Linux screen

Reached from Settings ("Linux…"), the actions menu (L3 / Shift+F10 / right-click) and
Ctrl+Shift+L. Same look as Settings; every control works with keyboard, mouse or controller alone,
with the usual input-aware hints, and all text is translated (21 languages).

```
PS5Term / Linux                                              Running · network on   [Stop Linux]

ENVIRONMENTS                                           ┌ Ubuntu 24.04 LTS ───────────────────────┐
 ▸ Base          built in · always there      Default   │ Installed · 412 MiB on disk             │
   Alpine 3.23   installed · 38 MiB                    │ Package manager: apt                    │
   Ubuntu 24.04  installed · 412 MiB                   │ [Open]  [Make default]  [Remove…]       │
   Debian 13     not installed · needs 600 MiB         │ Tip: apt is slower than apk here: the   │
                                                       │ whole system is emulated.               │
STORAGE  ▓▓▓▓▓▓▓▓░░░░░░  612 / 1024 MiB    [Expand…]   └─────────────────────────────────────────┘
PS5 FILES in /mnt/ps5: /data                [Change]
```

- **List** (left): every known environment with version, state and size on disk. Base is always
  first and cannot be removed.
- **Details** (right) for the focused one:
  - not installed: what it is, the download size and the space it needs, and **[Install]**
    (refused with the reason when free space is short, offering **Expand…**);
  - installing: a progress line (downloading n %, checking, unpacking) and **[Cancel]**;
  - installed: **[Open]** (opens it in the current tab, starting Linux if needed),
    **[Make default]**, **[Remove…]** (asks again; removes only that environment's folder).
- **Default**: the environment new Linux shells and `linux@` SSH logins open in. Base is the
  default until the user picks another, so nothing changes for anyone who doesn't. `exit` from an
  environment returns to Base, which stays the rescue shell; if the default fails to start,
  PS5Term opens Base with a one-line reason.
- **Storage**: a bar for the data disk with **Expand…** offering 1 GiB, 2 GiB and 4 GiB (each
  checked against the PS5's free space; it still keeps a backup and needs Linux stopped).
- **PS5 files** and **Start/Stop Linux** move here from Settings; Settings keeps a single
  "Linux…" entry and a status line.

## Environments offered

| Environment | Source (official, riscv64) | Download / on disk | Package manager | Notes |
|---|---|---|---|---|
| Alpine 3.23 | Alpine minirootfs | ~3 MB / ~10 MB + packages | apk | Fastest; already supported |
| Ubuntu 24.04 LTS | Ubuntu Base 24.04.x | ~31 MB / ~120 MB + packages | apt | 25.10+ needs RVA23 (vector); the emulator is RV64GC, so 24.04 only |
| Debian 13 | Debian riscv64 port | ~ / ~250 MB | apt | After Ubuntu, same mechanics |

Every entry pins a URL and SHA-256 in the guest image (like Alpine today). Nothing unverified is
unpacked. Distributions are downloaded by the user's request and are not distributed by PS5Term;
their own licenses apply.

## How it works

- **Guest side**: `ps5term-alpine` becomes `ps5term-env` (Alpine keeps working, and
  `ps5term-alpine` stays as a compatibility name). One manifest per environment (name, version,
  URL, SHA-256, space needed, marker file, shell, package-manager setup such as apk repositories or
  `APT::Sandbox::User`). Commands: `list --json`, `install <name>`, `remove <name>`,
  `enter <name>`, `run <name> <command>`, `default [<name>]`. Progress is printed as
  `progress <stage> <percent>` lines.
- **App side**: the Linux screen asks the guest for `ps5term-env list --json` through the existing
  guest agent (EXEC channel, as SSH commands use), so the screen shows real state; installs run
  in the background through the agent and stream progress. Opening an environment sends
  `ps5term-env enter <name>` to the tab's shell, as Open Alpine does now.
- **Default**: stored in `/mnt/data/.ps5term-env-default`; the guest's login profile enters it for
  interactive shells (local tabs and `linux@` SSH logins). Commands sent over SSH
  (`ssh linux@ps5 'cmd'`) run in Base, as scripts expect; `ssh linux@ps5 'ps5term-env run ubuntu
  apt update'` targets an environment explicitly.
- **Disk**: raise the data-disk limit from 1 GiB to 4 GiB (bounded by the PS5's free space) and
  make the expansion size a choice.
- **Memory**: the guest has 128 MiB. If the Ubuntu prototype shows `apt` struggling, add a
  256 MiB option (costs the app memory; measured before offering).

## Testing

Host: manifest/checksum tests, `ps5term-env` in the existing guest emulator tests (install from a
local archive, enter, run, remove, default, disk-full refusal), app tests for every control with
each input, all 21 languages rendered and checked for overflow. Hardware: install, open, apt or
apk update and a real package, default + `linux@` SSH, remove, on the phat.

## Phases

1. Ubuntu 24.04 prototype on the phat (feasibility, timings, memory, disk).
2. `ps5term-env` with Alpine + Ubuntu, larger expansion sizes.
3. The Linux screen, translations, tests, hardware check, release.
4. Debian, if wanted.
