#!/usr/bin/env bash
# Linux guest-only component acceptance. No network, console or image mutations.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
if [[ $(uname -s) != Linux ]]; then
    echo 'Run inside the existing Linux build container; this tests Linux PTYs.' >&2
    exit 2
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d /tmp/ps5term-agent-check.XXXXXX)
trap 'rm -rf -- "$work"' EXIT
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror)
if [[ ${SANITIZE:-1} == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${HOST_CC:-cc}" "${flags[@]}" "$root/tooling/subsystem/guest-agent/agent.c" -o "$work/agent"
ASAN_OPTIONS=detect_leaks=1 PS5TERM_GUEST_AGENT="$work/agent" \
    python3 -W error "$root/tests/subsystem/guest_agent/test_agent.py" -v
