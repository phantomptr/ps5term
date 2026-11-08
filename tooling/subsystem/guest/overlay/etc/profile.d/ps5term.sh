# SPDX-License-Identifier: GPL-3.0-or-later
# Guest-local defaults. /mnt/data survives restarts; everything else is temporary.
export TERM="${TERM:-xterm-256color}"
export LANG="${LANG:-C.UTF-8}"
export PS1='linux:\w # '
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
if grep -q ' /mnt/data ' /proc/mounts 2>/dev/null; then
    export HISTFILE=/mnt/data/.ash_history
    mkdir -p /mnt/data/bin 2>/dev/null
    case ":$PATH:" in
        *:/mnt/data/bin:*) ;;
        *) export PATH="/mnt/data/bin:$PATH" ;;
    esac
    # Personal settings that should survive a restart.
    [ -r /mnt/data/profile ] && . /mnt/data/profile
fi
