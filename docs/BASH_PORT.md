# Bash port status

Bash 5.3.20 is now reproducibly cross-compiled and linked against the pinned
PS5 Payload SDK. **It is not integrated, packaged, deployed, or tested on PS5.**
The Linux reference build passes `tests/ssh/full_shell_smoke.bash`: conditionals,
`command`, functions, arrays, `case`, expansion, command substitution, pipes,
heredocs, subshells and exit status. These are reference results only.

## Reproduce

Python 3.12+ and `patch` are needed for preparation. The build container supplies
Clang/LLD/LLVM 18, make and the pinned SDK; its checkout is mounted at `/work`.

```sh
python3.12 tools/prepare-bash.py --fetch
# Inside ps5term-build, from /work:
python3 tools/build-bash-port.py reference --output build/bash-port/reference-new
build/bash-port/reference-new/bash --noprofile --norc tests/ssh/full_shell_smoke.bash
python3 tools/build-bash-port.py ps5 --output build/bash-port/ps5-new
```

Existing source/build directories are refused, preserving experiments. Use fresh
output names or deliberately move old trees. Fetches verify SHA256 before writing;
all archives are verified before source preparation. No app files, SDK installation,
SSH configuration, or console state are modified. Source and 20 official GNU patch
hashes are pinned in `tooling/bash/sources.json`; hashes were recorded from GNU's
HTTPS origin, not independently authenticated with GPG.

## Deliberately limited first build

This is an SDK **link audit**, using SDK startup code. It is not the child startup
adapter that the homebrew app needs. No Readline, job control, multibyte editing or
network redirections are enabled. The reference build retains its host's multibyte
support; its OS process support is not evidence about the PS5.

`ps5-portability.patch` corrects the fallback dprintf declaration to match its
existing int implementation, guards a multibyte-only printf variable, exposes the
locale flag used by the byte-oriented glob matcher, and skips the unavailable
ttyname fallback in the PS5-only no-terminal profile. Target fputs_unlocked calls
use the SDK's locking fputs implementation. No fabricated successful syscalls or
privilege changes are used. GNU sources and their licenses remain intact.

## Remaining integration gates

1. App-owned child launch, data exchange, exec, exit status, cancellation and reap.
   The native capability record still reports EPERM for tested rfork variants.
2. Child startup/environment and resource ownership, independent of the loader's
   startup context. Keep Bash in its own process: it changes cwd, descriptors,
   signals and global interpreter state, and cannot safely become an app thread.
3. Run the smoke suite on phat from the app-owned adapter, then test filesystem,
   libc locale/stdio and signal behavior. Header/link availability proves none of
   these runtime semantics.
4. Real PTY, resize, terminal modes and process groups before interactive Readline,
   foreground job control, editors or tmux can be advertised.
5. Route local and owned SSH sessions through that same lifecycle. Bundle the shell
   and credited sources in the app release after these gates pass. No separate
   payload should be required from the user.

The original FreeBSD sh candidate remains useful for a smaller POSIX baseline;
Bash is added specifically for the language/features expected by Warp bootstrap.
Do not reproduce its parser inside the limited built-in shell.

## Warp boundary

A Bash port addresses shell-language compatibility. Warp's current remote extension
requires Linux/macOS, Bash/Zsh and other OS facilities; a native PS5 Bash binary
cannot run its Linux/macOS companion server. Plain SSH remains the immediate path:
`command ssh root@192.168.86.99` opts out of the legacy wrapper. Enhanced integration
needs its own interoperability test and must not be advertised as solved.

Sources: [GNU patches](https://ftp.gnu.org/gnu/bash/bash-5.3-patches/),
[Warp requirements](https://docs.warp.dev/terminal/warpify/ssh/),
[legacy wrapper](https://docs.warp.dev/terminal/warpify/ssh-legacy#implementation).
PacBrew tree `722187f0ee246c0d7d57401c8a97de1c7f4e2ff0` had no Bash,
Readline, ncurses or BusyBox recipe when audited for this port.
