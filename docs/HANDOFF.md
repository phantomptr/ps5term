# Handoff

For the current state, read `README.md`, `docs/FEATURES.md`, `docs/SUBSYSTEM.md` and
`docs/DEVELOPMENT_LOOP.md`. The rolling work checkpoint is `.local/dev/CHECKPOINT.md`,
which is not committed. The notes below are history from 2026-11-01; their test status
and SSH notes are out of date.

# Handoff: mouse and keyboard work (2026-11-01)

State of PS5Term after the mouse and keyboard work done alongside ps5upload. Read this
first, then `docs/MOUSE.md` (what the mouse does and how it is built).

## Status

- **Done and installed on the fat PS5 (FW 13.60):** mouse support as described in
  `docs/MOUSE.md`. The installed `eboot.bin` (sha256 `7fe1bb3b…`) is byte-identical to a
  fresh build of the current source (checked 2026-11-01).
- **Tests pass on Linux:**
  `docker exec ps5term-build bash -lc 'cd /work && HOST_CXX=clang++-18 HOST_CC=clang-18 bash tools/test-terminal.sh'`
  The container has no plain `clang++`, so `make test-terminal` fails there.
- **On macOS** `make test-terminal` stops at `tests/terminal/test_settings.cpp:11`: its stand-in
  `access()` lacks the `noexcept` that macOS declares. That test belongs to the SSH/settings
  work, not the mouse work. It has not been fixed.
- **Proven on hardware** with a real virtual mouse and keyboard (from ps5upload's Remote Play
  page): keys typed into the terminal, pointer movement, left click, right-click menu and
  wheel events reached the app.
- **Not yet tried by the user on hardware:**
  - drag-select and copy/paste;
  - word and line clicks;
  - wheel scrolling of long output;
  - clicks in Settings and the mouse-speed buttons;
  - on-screen keyboard clicks.

  The host tests cover all of these.

## Rules for this repo

- **No git history yet.** Never `git clean` / `git reset`. Don't commit unless the user asks.
  A first commit is the user's call.
- **SSH code** (`src/ssh_service.*`, `docs/SSH*.md`, `/data/ps5term/ssh`) was being changed in a
  Codex session. Leave it alone unless the user asks.
- **Deploy without launching. The user opens PS5Term from the home screen.**
  - Never use ps5upload's engine app launch for FAKE99260. It fails with `0x80aa001a` and falls
    back to restarting ShellUI.
  - Run `python3 .local/deploy-nolaunch.py` from the repo root, with the ps5upload desktop app
    running (engine on 127.0.0.1:19113).
  - The script stops PS5Term if it is running, copies `dist/FAKE99260` to
    `/data/homebrew/FAKE99260`, and checks every file by sha256.
  - It targets the fat PS5 at 192.168.86.99. Edit `addr` for the Pro (.100).
- **Ask the user before any hardware test.**

## Build

```
docker exec ps5term-build bash -lc 'cd /work && bash tools/build.sh && python3 tools/package-app.py'
```

The output is `dist/FAKE99260` plus `dist/PS5Term-00.001.000.zip`.

## Launch traps (each one cost a broken launch)

- **Imports `sceKernelLoadStartModule` / `sceKernelDlsym`:** the app exits at launch. Link Sony
  functions directly through a stub `.so`; `tools/make-stub.sh` makes `libSceMouse.so` on
  every build.
- **CE-107750-0 = the loader refused the ELF:** a `.data.rel.ro` section misaligned the writable
  segment in the original converter. The converter now accounts for pointer tables before
  `.got`; regression tests and a phat launch passed. The LOAD alignment guard remains.
  See `docs/MOUSE.md`.
- **The research code** (`src/research.cpp`) compiles only with
  `APP_DEFINITIONS=PS5TERM_RESEARCH`. It is stubbed out in normal builds, because its imports
  caused the exit above.

## Saved builds and leftovers (`.local/`)

| Path | What it is |
|---|---|
| `known-good/` | Last build before the mouse work. Deploy it to roll back. |
| `monitor-build/` | Research build with the virtual-device monitor. |
| `mouse-build/` | The current mouse build, the one installed. |
| `mouse-build-failed/` | The CE-107750-0 build, kept as evidence for the alignment check. |
| `pre-mouse-originals/` | The files changed by the mouse work, as they were before it. |

`research/vdev/` is the virtual mouse and keyboard probe payload. That work now lives in
ps5upload (the `ps5u-vinput` companion, branch `kbm-test`), so PS5Term no longer needs it.

## How the virtual mouse and keyboard reach PS5Term

ps5upload's Remote Play page captures the computer's keyboard and mouse. It sends them through
the ps5upload engine and helper to a small companion payload, which creates virtual
libSceMouse/libSceKeyboard devices on the console. To PS5Term these look exactly like a USB
mouse and keyboard, so it needs nothing special for Remote Play.

The companion is local-only for now: ps5upload branch `kbm-test` in `../ps5upload-kbmtest`,
FW 13.60, with `/data/ps5upload/vinput.enable` present. That side stays with the ps5upload
work. To test PS5Term with a mouse, use a USB mouse on the PS5, or ask for the ps5upload test
app and companion to be running.

The diagnostics file `/data/ps5term/mouse.debug` (it turns on `mouse.log`) has been removed
from the console.

## Ideas not started

- Mouse-driven tabs or panes, if the app gets them.
- Hover highlight on links or paths in the output.
- Saving the clipboard across restarts.
