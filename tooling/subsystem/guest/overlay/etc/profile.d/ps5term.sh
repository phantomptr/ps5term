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
# The environment chosen with "ps5term-env default <name>" opens for new interactive shells.
# exit returns here, to the built-in shell; never nested, never for scripts.
case $- in
    *i*)
        if [ -z "${PS5TERM_ENV:-}" ] && [ -r /mnt/data/.ps5term-env-default ]; then
            # Asked of ps5term-env, which answers base for a removed or damaged choice.
            ps5term_default=$(ps5term-env default 2>/dev/null)
            if [ -n "$ps5term_default" ] && [ "$ps5term_default" != base ]; then
                ps5term-env enter "$ps5term_default" ||
                    echo "PS5Term: could not open $ps5term_default; this is the built-in Linux shell."
            fi
            unset ps5term_default
        fi
        ;;
esac
