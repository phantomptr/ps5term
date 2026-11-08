#!/bin/sh
# PS5Term - ps5term-env on any riscv64 Linux with root (the guest, or an emulated container).
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   sh tests/guest/test-ps5term-env.sh <ps5term-env> <alpine-minirootfs-riscv64.tar.gz>
#
# The archive is Alpine's official riscv64 minirootfs (URL and SHA-256 in ps5term-env). From
# macOS or Linux with Docker's riscv64 emulation, with the archive saved in the repository:
#   docker run --rm --privileged --platform linux/riscv64 -v "$PWD:/work:ro" alpine:3.23 \
#     sh /work/tests/guest/test-ps5term-env.sh /work/tooling/subsystem/guest/overlay/usr/bin/ps5term-env \
#     /work/alpine-minirootfs-3.23.8-riscv64.tar.gz
# It uses a fresh tmpfs at /mnt/data, so nothing real is touched.
set -eu
env_tool=$1 archive=$2
fail() { echo "FAIL: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || fail 'needs root (mount, chroot)'
! grep -q ' /mnt/data ' /proc/mounts || fail '/mnt/data is already mounted; run in a throwaway system'
mkdir -p /mnt/data /usr/local/bin
mount -t tmpfs -o size=96m tmpfs /mnt/data
trap 'umount -l /mnt/data 2>/dev/null || true' EXIT
cp "$env_tool" /usr/local/bin/ps5term-env
chmod 755 /usr/local/bin/ps5term-env
export PATH=/usr/local/bin:$PATH

# Nothing installed: Base is the default and the report is the app's JSON.
json=$(ps5term-env list --json)
echo "$json" | grep -q '^{"default":"base","data":true,"disk_kib":[0-9]*,"used_kib":[0-9]*,"environments":\[' ||
    fail "list --json: $json"
echo "$json" | grep -q '"name":"alpine","title":"Alpine 3.23","manager":"apk","state":"absent","kib":0' ||
    fail "alpine not absent: $json"
echo "$json" | grep -q '"name":"ubuntu",.*"state":"absent"' || fail "ubuntu not absent: $json"

# A file that is not the pinned archive is refused and leaves nothing behind.
head -c 4096 /dev/urandom >/tmp/not-alpine.tar.gz
! ps5term-env install alpine /tmp/not-alpine.tar.gz 2>/dev/null || fail 'accepted a wrong checksum'
[ -z "$(ls -A /mnt/data)" ] || fail "leftovers after a refused install: $(ls -A /mnt/data)"
! ps5term-env default alpine 2>/dev/null || fail 'made an absent environment the default'
! ps5term-env install debian 2>/dev/null || fail 'accepted an unknown name'

# Install from the verified archive, saying what it does.
ps5term-env install alpine "$archive" >/tmp/install.out
grep -q '^Checking the download...$' /tmp/install.out &&
    grep -q '^Alpine 3.23 installed' /tmp/install.out || fail "install output: $(cat /tmp/install.out)"
[ -f /mnt/data/alpine/etc/alpine-release ] || fail 'no marker file'
[ "$(cat /mnt/data/alpine/etc/apk/repositories | head -1)" = \
    https://dl-cdn.alpinelinux.org/alpine/v3.23/main ] || fail 'apk repositories not set'
! ls /mnt/data/.ps5term-env-* 2>/dev/null || fail 'staging files left'
! ps5term-env install alpine "$archive" 2>/dev/null || fail 'installed twice'
ps5term-env list --json | grep -q '"name":"alpine",[^}]*"state":"installed","kib":0,' ||
    fail "quick list: $(ps5term-env list --json)"
ps5term-env list --json --sizes | grep -q '"name":"alpine",[^}]*"state":"installed","kib":[1-9]' ||
    fail "sizes: $(ps5term-env list --json --sizes)"

# Commands run inside it, with its own system and the binds in place.
[ "$(ps5term-env run alpine cat /etc/alpine-release)" = 3.23.8 ] || fail 'run alpine'
[ "$(ps5term-env run alpine sh -c 'echo $PS5TERM_ENV')" = alpine ] || fail 'PS5TERM_ENV'
ps5term-env run alpine test -e /proc/self/status || fail '/proc not bound'
! ps5term-env run alpine 2>/dev/null || fail 'run without a command'

# Default: set, reported, and only for installed environments.
ps5term-env default alpine >/dev/null
[ "$(ps5term-env default)" = alpine ] || fail 'default not saved'
ps5term-env list --json | grep -q '^{"default":"alpine"' || fail 'default not listed'

# Removing refuses while something runs inside, then removes only its folder.
touch /mnt/data/keep-me
ps5term-env run alpine sleep 30 &
inside=$!
sleep 2
! ps5term-env remove alpine 2>/tmp/remove.err || fail 'removed while in use'
grep -q 'still in use' /tmp/remove.err || fail "in-use message: $(cat /tmp/remove.err)"
[ -f /mnt/data/alpine/etc/alpine-release ] || fail 'damaged while refusing'
kill "$inside" 2>/dev/null || true
wait "$inside" 2>/dev/null || true
for _ in 1 2 3 4 5; do pgrep -x sleep >/dev/null || break; sleep 1; done
ps5term-env remove alpine >/dev/null
[ ! -e /mnt/data/alpine ] || fail 'folder still there'
[ -f /mnt/data/keep-me ] || fail 'removed something else'
! grep -q ' /mnt/data/alpine/' /proc/mounts || fail 'binds left mounted'
[ "$(ps5term-env default)" = base ] || fail 'default not reset to base after removal'
ps5term-env list --json | grep -q '"name":"alpine",[^}]*"state":"absent"' || fail 'still listed'

# Not enough room: refused before downloading anything.
mount -o remount,size=40m /mnt/data
! ps5term-env install alpine "$archive" 2>/tmp/space.err || fail 'installed without room'
grep -q 'needs 64 MiB free' /tmp/space.err || fail "space message: $(cat /tmp/space.err)"
echo 'PASS: ps5term-env list, verified install, run, default, in-use refusal, remove, space check'
