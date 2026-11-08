#!/usr/bin/env bash
# ps5-native-app-boilerplate - Host test dependency bootstrapper.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetches a verified GoogleTest source release into the ignored dependency cache.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version="1.17.0"
directory="$root/.deps/test/googletest"
source="$directory/googletest-$version"
archive="$directory/googletest-$version.tar.gz"
stamp="$directory/.source-sha256"
url="https://github.com/google/googletest/archive/refs/tags/v$version.tar.gz"
hash="65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c"

# macOS ships a sha256sum without GNU's long options, so verify by comparing digests.
digest() {
    if command -v shasum >/dev/null; then shasum -a 256 "$1"; else sha256sum "$1"; fi | cut -d ' ' -f 1
}
for command in wget tar; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done

mkdir -p "$directory"
if [[ -f $archive ]] &&
    [[ $(digest "$archive") != "$hash" ]]; then
    rm -f -- "$archive"
fi
if [[ ! -f $archive ]]; then
    temporary="$archive.download"
    rm -f -- "$temporary"
    wget -q "$url" -O "$temporary"
    [[ $(digest "$temporary") == "$hash" ]] || { echo "GoogleTest archive hash mismatch" >&2; exit 2; }
    mv "$temporary" "$archive"
fi

if [[ ! -f $source/googletest/include/gtest/gtest.h ||
    ! -f $stamp || $(<"$stamp") != "$hash" ]]; then
    echo "==> [test-deps] Extracting GoogleTest $version" >&2
    rm -rf -- "$source"
    tar -xzf "$archive" -C "$directory"
    printf '%s\n' "$hash" >"$stamp"
fi

[[ -f $source/googletest/src/gtest-all.cc &&
    -f $source/googletest/src/gtest_main.cc ]] || {
    echo "the pinned GoogleTest source tree is incomplete" >&2
    exit 2
}
printf '%s\n' "$source"
