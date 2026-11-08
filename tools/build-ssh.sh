#!/usr/bin/env bash
# PS5Term - native SSH payload. SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=${PS5_PAYLOAD_SDK:-$root/.deps/backend/ps5-payload-sdk}
[[ -x "$sdk/bin/prospero-clang" ]] || {
    echo 'Set PS5_PAYLOAD_SDK to an installed v0.45 SDK (for example /opt/ps5-payload-sdk).' >&2
    exit 2
}
export PS5_PAYLOAD_SDK="$sdk"
export LLVM_CONFIG=${LLVM_CONFIG:-$(command -v llvm-config-18)}
python3 "$root/tools/prepare-ssh.py"
make -C "$root/build/ssh" -j2 \
    WOLFSSL_DIR="$root/.deps/ssh/wolfssl-5.9.4-stable" \
    WOLFSSH_DIR="$root/.deps/ssh/wolfssh-1.6.0-stable" \
    SHSRV_DIR="$root/build/ssh/shsrv"
output="$root/dist/ssh"
mkdir -p "$output/licenses"
cp "$root/build/ssh/ps5sshd.elf" "$output/ps5sshd.elf"
cp "$root/third_party/ps5sshd/COPYING" "$output/licenses/ps5sshd-GPL.txt"
cp "$root/.deps/ssh/wolfssl-5.9.4-stable/COPYING" "$output/licenses/wolfSSL-GPL.txt"
cp "$root/.deps/ssh/wolfssh-1.6.0-stable/LICENSING" "$output/licenses/wolfSSH-LICENSING.txt"
cp "$root/third_party/shsrv/LICENSE" "$output/licenses/shsrv-GPL.txt"
cp "$root/docs/SSH.md" "$output/README.md"
cp "$root/docs/SSH-SOURCES.md" "$output/SOURCES.md"
mkdir -p "$root/assets/backend" "$root/assets/licenses/ssh"
cp "$output/ps5sshd.elf" "$root/assets/backend/ps5sshd.elf"
cp "$output/licenses/"* "$root/assets/licenses/ssh/"
cp "$root/docs/SSH-SOURCES.md" "$root/assets/licenses/ssh/SOURCES.md"
echo "Built internal SSH server asset for PS5Term (hardware validation pending)"
