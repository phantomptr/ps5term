# Modern Linux candidate

This BR2_EXTERNAL tree targets the owned TinyEMU RISC-V interpreter, with 128 MiB
RAM, a 16 MiB ephemeral ext2 disk, musl, BusyBox, less, ncurses/terminfo, curl with
Mbed TLS, nano, tmux, htop, jq, tree and the PTY agent (`package/ps5term-agent`). Its
overlay configures the app's NAT by DHCP, the persistent disk, the PS5 files mount and
the agent, each only when the app provides the device. Guest root is separate from the
PS5 process identity. It is not an Ubuntu distribution and has no binary package
manager: additions are selected in this Buildroot configuration and rebuilt.

The source archive is Buildroot 2025.02.19 LTS, pinned in
`tools/dev/modern_guest.py`. It selects Linux 6.12.112, OpenSBI 1.6 and GCC 13.4.0.
Component download hashes come from that pinned Buildroot tree. The Buildroot
archive pin was observed from official HTTPS; its PGP signature has not been
verified. Do not claim bit-for-bit reproducibility until a second clean build
has been compared.

Use the existing Ubuntu 24.04 development container on macOS, with this repository
mounted at `/work`. Install `build-essential bc bison flex cpio rsync file
xz-utils libncurses-dev python3 perl ca-certificates wget unzip patch`, then run
as an unprivileged build user, on the container's Linux-native filesystem:

```sh
cd /work
bash tools/build-modern-guest.sh /tmp/ps5term-modern-candidate
```

The recipe uses two workers, a fixed SOURCE_DATE_EPOCH and a source/configuration
fingerprint. An interrupted build can resume with the same command. Changed
inputs require a fresh directory; existing outputs are never automatically
removed. Keep the container until its useful output has been copied out.

Buildroot's source tar contains one absolute symlink in an unused OpenRC target
skeleton. The extractor validates all members, allows only that exact reviewed
absolute target, refuses members beneath links, writes ordinary files first,
and creates links last. Extraction is into a fresh disposable Linux directory.
It never restores owners or special permissions.

After the build, `output/legal-info` contains upstream source/license archives
and manifests. Review its warnings and include the corresponding source delivery
with any release; producing images alone is not license completion. Preserve
Buildroot source, this external tree, generated configuration, patches and build
commands as well. Source bundles can be much larger than the guest disk.

Stage only an experimental candidate after the image header/size checks:

```sh
python3 -m tools.dev.guest_images --images /tmp/ps5term-modern-candidate/output/images \
    --destination /tmp/ps5term-modern-candidate/candidate
```

The existing API filenames are retained (`bbl64.bin` now holds OpenSBI fw_jump.bin,
not the historical bbl). The manifest explicitly records unverified host/PS5
runtime status. The raw RISC-V Image must load 2 MiB above the firmware, at
0x80200000. See [Linux's header specification](https://docs.kernel.org/arch/riscv/boot-image-header.html)
and [OpenSBI FW_JUMP](https://github.com/riscv-software-src/opensbi/blob/v1.6/docs/firmware/fw_jump.md).

`tests/subsystem/test_modern_guest.c` must pass against these images before UI
integration or phat deployment. It checks the actual kernel version, shell
composition, fork/wait, PTYs, resize, terminfo, repeated boot and immutable source
disk. Local tests cannot establish PS5 runtime compatibility or other firmware
support. No old upstream research guest is part of a release.

Host acceptance on 2026-11-02 succeeded with the owned interpreter on macOS
arm64 using ASan/UBSan. The modern firmware exposed three compatibility gaps in
the original 2019 core: missing HTIF MMIO addresses in the device tree, missing
read-only CPU identification CSRs, and a discarded TIME counter enable bit.
`tinyemu-embedded-profile.patch` supplies the device description and unspecified
zero CPU IDs, and preserves TIME access for OpenSBI's existing trap emulation.
These changes retain privilege and read-only CSR checks. Early SBI output is
available for diagnosis, but Linux's regular console must be virtio `hvc0`:
`HVC_RISCV_SBI` stays disabled so it cannot take that device number.

The verified candidate lives in local build artifacts, not `assets/` or a store
release. Runtime evidence records exact image and source hashes. To repeat:

```sh
python3 -m tools.dev.test_modern_guest --runtime build/embedded-modern-time-darwin-asan-v2 \
    --images build/modern-guest-virtio-candidate \
    --output build/modern-guest-new-acceptance
```

Use a fresh output directory. An existing interpreter build must match current
adapter and patch hashes, otherwise rebuild it with
`python3 -m tools.dev.build_embedded_runtime --sanitize --output build/new-runtime`.
The recipe's configuration and safe extraction have been exercised, but the
initial complete image was built with explicit Buildroot make invocations and
subsequent reconfiguration; a clean end-to-end recipe rebuild and hash comparison
remain release gates. Host success does not enable the native app's subsystem.
