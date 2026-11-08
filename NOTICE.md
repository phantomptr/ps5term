# Notices

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The application does not redistribute or dynamically load the
complete libc++ or libc++abi archives.

The project’s PS5 ELF converter and FSELF writer are independently authored
GPL-3.0-or-later code. SharpProspero was a useful public format reference during
development but is not fetched, copied, linked, or required by the build.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

## Optional UFS2Tool dependency

When `.ffpkg` output is requested, the platform bootstrapper fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77` into the ignored
`.deps/UFS2Tool` cache and builds it with the host .NET SDK. UFS2Tool is
BSD-2-Clause software and is not distributed by this repository.

## Optional MkPFS dependency

When `.ffpfsc` output is requested, the platform bootstrapper fetches
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) at commit
`6cb8313dfe0c988ac52617794553f343243d3a56` into the ignored `.deps/MkPFS`
cache and installs its Python dependencies into an ignored virtual environment
there. MkPFS and its dependencies retain their own licenses and are not
distributed by this repository.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Original presentation assets

The BlackBear icon, selection artwork, and default selection track
`sce_sys/snd0.at9` are original assets supplied by BlackBearReloaded, Copyright
(C) 2026 BlackBearReloaded, and distributed under GPL-3.0-or-later. The track
is titled `Night Drive`.

No proprietary runtime module, encryption key, or game file is included.

## PS5Term components

The portable VT engine vendors libvterm 0.3.3 by Paul Evans under the MIT
license, preserved at `third_party/libvterm/LICENSE`. Its official source archive
is `https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz`, SHA-256
`09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`.
The unmodified `include/` and `src/` files and their hashes are recorded in
`third_party/libvterm/source.json`; `tools/verify-vterm.py` checks them before
host or native compilation. No upstream binaries are included.

PS5Term includes terminal, input, settings and service lifecycle code under
GPL-3.0-or-later. The local command backend derives from shsrv by John Törnblom.
The legacy SSH service derives from intestinate/ps5sshd, wolfSSH and wolfSSL.
The optional owned service uses wolfSSH/wolfSSL directly with PS5Term-owned
authentication, commands and native filesystem adapters. Its SFTP implementation
uses upstream wolfSSH with bounded-directory and native portability patches.
PS5Term also applies the reproducible `tooling/ssh/channel-exit-status.patch`
(per-channel, retry-safe exit status) and `session-channel-limit.patch` (an opt-in
session cap). These are project modifications, not claims about upstream APIs.
See docs/SSH-SOURCES.md and third_party source headers for provenance and licenses.
The bitmap font in third_party/font8x8/font8x8_basic.h is public-domain font8x8
from https://github.com/dhepper/font8x8, based on public-domain IBM VGA fonts.


## Research executable capability metadata

The optional `rfork-research` profile uses the public native-executable baseline
record in [EchoStretch/kstuff-lite](https://github.com/EchoStretch/kstuff-lite/blob/48cd2c4ce62aa9ca0c2cc014c1e512f276ae7c17/ps5-kstuff/uelf/fself.c),
credited to the kstuff contributors. It changes one capability bit for an
explicitly labelled child experiment. No kstuff runtime, kernel code, private
credential, signature or dump is embedded in PS5Term. Default builds do not
include this override. Compatibility outside the measured loader/firmware
combination is unverified.

## Native directory API investigation

The 64 KiB minimum directory-buffer requirement was identified by comparing
our app0 EINVAL result with the directory notes in
[Phoenixx1202/kodi-ps5 libc_posix.c](https://github.com/Phoenixx1202/kodi-ps5/blob/main/shims/native-app/libc_posix.c).
Thanks to that project's contributors. PS5Term uses its own bounded cursor and
bytewise SDK-record decoder; no Kodi shim source is included.

## Optional GNU Bash port

GNU Bash 5.3 with official patches 001-020 is authored by the Free Software
Foundation and its contributors, distributed under GPL-3.0-or-later. Sources are
fetched from GNU using `tooling/bash/sources.json`; upstream COPYING and notices
remain in the prepared source. PS5Term's small portability patch and isolated
preparation/build tools are in tooling/bash and tools. The experimental binary
is not included in the deployed app. See docs/BASH_PORT.md.

## Host-only embedded Linux experiment

TinyEMU is authored by Fabrice Bellard and its contributors, under the MIT
license retained in its downloaded source. PS5Term's preparation and
smoke-test tools fetch pinned official archives and apply the small portability
profile in tooling/subsystem. The experimental subsystem embeds the interpreter
and a separately built modern guest; the old sample is not included. The upstream 2018 Linux sample is research material only. See
docs/research/2026-11-01-embedded-linux-results.md for provenance and limitations.
The experimental in-process adapter is in backend/subsystem. Its embedding patch
preserves upstream notices and adds assertion recovery, a zero-length FDT copy
guard and retryable console descriptor backpressure. The owned worker/session layer is integrated behind the experimental Linux
build definition, with separate host and phat firmware 13.60 acceptance.

## Guest network backend (libslirp)

The `PS5TERM_LINUX_NETWORK` profile compiles a private subset of
[libslirp](https://gitlab.freedesktop.org/slirp/libslirp) 4.9.7 (QEMU project
and contributors; BSD-3-Clause, derived from Danny Gasparovski's SLiRP). The
source archive is kept at `third_party/libslirp`, taken from the official repository at
commit `ce12dcddf710a71dc0de6450ec95edbadf8e9f66`, SHA-256
`6b19073be7764feabfaa55543b4567577b1a004dae7bbfc405a384f36fedaf3c`; no
publisher signature is claimed. `tools/dev/prepare_slirp.py` records the exact
source edits of the profile: process spawning, Unix-domain forwarding, TFTP,
host resolver discovery and fuzzing hooks are removed, assertions report through
a C recovery boundary, and sockets go through PS5Term's owned policy. The GLib
dependency is replaced by a small project-authored compatibility header in
`backend/subsystem/net/`; no GLib code is included. The upstream LICENSE and
COPYRIGHT files ship with the app at `assets/linux/licenses/libslirp-4.9.7/`.
The guest receives IPv4 by DHCP from the in-app NAT; no inbound connection or
host loopback access is forwarded to the guest.

## JetBrains Mono presentation font

JetBrains Mono Regular is by the JetBrains Mono Project Authors, licensed under
the SIL Open Font License 1.1. The unmodified TTF, complete OFL notice and pinned
source/hash inventory are in third_party/jetbrains-mono. PS5Term embeds grayscale
glyph coverage for ASCII and selected Unicode ranges, generated by
tools/generate-font-atlas.py (Pillow 12.0.0, fontTools);
no FreeType/Pillow runtime or system font installation is required on the console.

## Modern Linux guest build recipe

The optional candidate recipe in tooling/subsystem/guest uses
[Buildroot](https://buildroot.org/) and its contributors' package definitions to
build [Linux](https://kernel.org/), [OpenSBI](https://github.com/riscv-software-src/opensbi),
[musl](https://musl.libc.org/), [BusyBox](https://busybox.net/),
[ncurses](https://invisible-island.net/ncurses/), less, e2fsprogs, curl, Mbed TLS,
CA certificates, nano, tmux, libevent, htop, jq, tree and a GNU cross-toolchain.
Their authors retain their copyrights and licenses. Upstream sources are not
rewritten as PS5Term code. Buildroot's legal-info target collects the selected
source archives, license texts and manifests; those outputs and the complete
build recipe require review and corresponding-source delivery before release.
The experimental Linux profile bundles these guest binaries. Local corresponding
guest sources, generated configurations, Buildroot archive and legal-info are
staged in build/modern-guest-source-delivery and must accompany public image
distribution. See docs/SUBSYSTEM.md. Native mapping API declarations follow the
OpenOrbis public libkernel header (OpenOrbis/OpenOrbis-PS4-Toolchain); no SDK
implementation or proprietary source is copied.

### Optional Alpine and Ubuntu userlands

`ps5term-env` downloads, on request, the official Alpine Linux 3.23.8 RISC-V minirootfs or
Ubuntu Base 24.04.5 riscv64, and verifies each archive's pinned SHA-256. Those archives and
the apk or apt packages installed later are not bundled in the app. Alpine, Ubuntu
(Canonical) and the package authors retain their copyrights and licenses; package metadata,
source and license information are at https://www.alpinelinux.org/,
https://pkgs.alpinelinux.org/, https://ubuntu.com/legal and https://packages.ubuntu.com/.
"Ubuntu" is a trademark of Canonical Ltd., named here only to identify the download. The
installer script is PS5Term GPL-3.0-or-later.

## PS5 programs (`run`)

`ps5term-run` (tooling/run-payload) starts programs with shsrv's `elfldr.c` and `pt.c` by John
Törnblom (GPL-3.0-or-later, third_party/shsrv). PS5Term ships the compiled `xterm-256color`
terminfo entry from ncurses 6.4 (Copyright Thomas E. Dickey and the Free Software Foundation,
X11/MIT-style license, see assets/linux/licenses) so ncurses programs find their terminal.
`tooling/run-payload/ps5term_tty.c` is offered under a permissive license so programs can
include it. The vendored libvterm 0.3.3 carries one local fix (REP and the pending wrap),
recorded in third_party/libvterm/source.json.

## Noto fonts for translated text

The app's translations in Chinese (Simplified and Traditional), Japanese, Korean, Arabic,
Persian, Hindi, Bengali and Thai are rendered ahead of time by `tools/generate-i18n.py` with
Noto Sans SC, TC, JP and KR (Copyright 2014-2021 Adobe, with Reserved Font Name 'Source') and
Noto Sans Arabic, Devanagari, Bengali and Thai (Copyright 2022 The Noto Project Authors), all
under the SIL Open Font License 1.1. Vietnamese uses JetBrains Mono. The fonts are fetched by
`tools/fetch-ui-fonts.py` from google/fonts at the commit and SHA-256 recorded in
`third_party/noto/SOURCE.json` and are not distributed; the app ships only the rendered text in
`assets/i18n/*.runs`. Each family's license is in `third_party/noto/`.
