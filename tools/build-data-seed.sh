#!/usr/bin/env bash
# GPL-3.0-or-later. Fresh immutable data-volume seed; never a user data file.
set -euo pipefail
if [[ $# != 2 || -e $2 || -L $2 ]]; then
    echo 'Usage: build-data-seed.sh /path/to/buildroot/host/sbin/mke2fs /fresh/output/data-seed.ext2' >&2
    exit 2
fi
# Stable UUID, directory hash seed and filesystem timestamps. The exact tool
# version must accompany the image pin; no cross-version bitwise claim.
umask 077
(set -o noclobber; : > "$2")
truncate -s 16M "$2"
E2FSPROGS_FAKE_TIME=1735689600 "$1" -q -F -t ext2 -b 1024 -I 256 -m 0 \
    -O none,filetype,sparse_super,large_file -U e6d76c02-7905-4aa2-a43f-1e5dbcc65b2e \
    -E lazy_itable_init=0,nodiscard,hash_seed=45b2e5f0-a2ed-47dd-a212-83313cab7802 \
    -L PS5TERM-DATA "$2"
