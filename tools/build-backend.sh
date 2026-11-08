#!/usr/bin/env bash
# PS5Term - pinned native shell payload, GPL-3.0-or-later.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache="$root/.deps/backend"
mkdir -p "$cache" "$root/build"
sdk="$cache/ps5-payload-sdk"
if [[ ! -x "$sdk/bin/prospero-clang" ]]; then
    wget -q https://github.com/ps5-payload-dev/sdk/releases/download/v0.45/ps5-payload-sdk.zip -O "$cache/sdk.zip.download"
    printf '%s  %s\n' 5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c "$cache/sdk.zip.download" | sha256sum -c -
    mv "$cache/sdk.zip.download" "$cache/sdk.zip"
    unzip -qo "$cache/sdk.zip" -d "$cache"
fi
python3 "$root/tools/prepare-backend.py"
export PS5_PAYLOAD_SDK="$sdk"
export LLVM_CONFIG="$(command -v llvm-config-18)"
make -C "$root/build/backend" -j2
mkdir -p "$root/assets/backend"
cp "$root/build/backend/shsrv-ps5.elf" "$root/assets/backend/ps5term-sh.elf"
printf '%s\n' 'Built dedicated loopback shell: 127.0.0.1:2324'
