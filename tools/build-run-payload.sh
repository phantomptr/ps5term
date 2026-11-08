#!/usr/bin/env bash
# PS5Term - build the ps5term-run payload (and its test program) with the pinned PS5 payload
# SDK, in the Linux build container. SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache="$root/.deps/backend"
sdk="$cache/ps5-payload-sdk"
mkdir -p "$cache" "$root/build/run-payload" "$root/assets/backend"
if [[ ! -x "$sdk/bin/prospero-clang" ]]; then
    wget -q https://github.com/ps5-payload-dev/sdk/releases/download/v0.45/ps5-payload-sdk.zip -O "$cache/sdk.zip.download"
    printf '%s  %s\n' 5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c "$cache/sdk.zip.download" | sha256sum -c -
    mv "$cache/sdk.zip.download" "$cache/sdk.zip"
    unzip -qo "$cache/sdk.zip" -d "$cache"
fi
export PS5_PAYLOAD_SDK="$sdk"
make -C "$root/tooling/run-payload" OUT="$root/build/run-payload" -B
cp "$root/build/run-payload/ps5term-run.elf" "$root/assets/backend/ps5term-run.elf"
sha256sum "$root/assets/backend/ps5term-run.elf" "$root/build/run-payload/ps5term-term-test.elf"
