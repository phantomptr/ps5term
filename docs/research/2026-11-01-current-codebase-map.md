# Current PS5Term codebase map

> Historical research snapshot, not a current feature or deployment guide.
> Start with [development scope](../DEVELOPMENT_SCOPE.md) and
> [current feature access](../FEATURES.md); retain the dated evidence below.

Snapshot: 2026-11-01. Read-only inspection of the app, build/preparation scripts, vendors, tests, handoff and existing artifacts; this document is the only addition from the scan. No build, test execution, deployment or console access was performed. The directory is an uncommitted worktree; do not clean/reset it. Generated outputs and historical records can differ from current source.

## Product and directory inventory

| Area | Current contents and role |
| --- | --- |
| `src/` | 25 files, about 125 KB: native renderer, app, terminal/editor, controller/keyboard/mouse input, pointer/clipboard UI, shell connection, SSH startup, settings and research hooks |
| `backend/` | Bounded single-command argument parser used by the adapted local shell |
| `third_party/shsrv/` | Vendored shell server, payload launch support and core command sources |
| `third_party/ps5sshd/` | Vendored SSH/SFTP server, authentication, configuration, sessions and shell adapter |
| `tools/` | Build, dependency setup, source adaptation, packaging, catalog record, deployment, lint/test and preview orchestration |
| `tooling/native/` | Project-owned ELF/FSELF converter, module writer, runtime generator, startup/C++ support, linker script and import/export manifests |
| `runtime/` | Generated `libc.prx`, digest and explanation; loader compatibility shim, not a complete libc |
| `assets/`, `sce_sys/` | Branding, two bundled server ELFs, license notices, title metadata and store icon |
| `tests/` | Renderer GoogleTest suite, Python tooling integration suite and six terminal-related executable suites |
| `research/` | Virtual input probe and import-stub research, separate from the normal app |
| `docs/` | Usage, template/platform instructions, latest input handoff and platform/environment research |
| `.deps/` | SDKs, crypto sources, zlib and an exploratory Lapy checkout; a cached project is not necessarily integrated |
| `.local/` | Private deployment scripts, historical validation reports and saved good/failed/research builds |
| `build/`, `dist/` | Generated intermediate files, current FAKE title folder/archive, and older PPSA/SSH outputs |

Current identity is `FAKE99260`, content version `00.001.000`. The worktree has no commits. No `.github/workflows/` files are present, despite inherited testing documentation describing GitHub Actions.

## Runtime flow

`main.cpp` initializes SceNet and starts the native drawing loop. Each frame calls `App::tick()` and draws the app. `App` owns one screen/editor, one shell connection, one SSH service state machine, settings and device/UI state.

The local shell connection first tries loopback port 2324. If unavailable, it sends packaged `/app0/assets/backend/ps5term-sh.elf` to an external ELF loader on loopback 9021, then retries the shell port. It uses Telnet framing and bounded nonblocking I/O; commands are submitted as a line, while interrupt uses Telnet IP. The app does not yet own the loader bootstrap.

SSH separately probes ports 22 and 2222, accepts its PS5Term identification banner, or sends the bundled SSH ELF to the same external loader. It periodically rechecks readiness. The enabled preference is a file under `/data/ps5term`; the adapted server watches that flag and includes session-termination logic. The UI's readiness result is a banner/listener check, not a new authenticated SSH test.

## Terminal and input

The screen is a 108-column, 512-line ring of byte characters with eight basic foreground colors. The escape parser supports selected clear/erase/color operations. It does not implement a complete VT terminal, UTF-8 cell widths, cursor-addressable full-screen applications or an alternate screen. The editor has a 512-byte line and 32-entry history.

Keyboard/controller actions currently drive this app-level line editor. They are not a general raw input stream for arbitrary interactive programs. Controller navigation, on-screen typing, submit/cancel/interrupt and Settings are implemented. Keyboard mapping includes editing, history, shortcuts and clipboard actions.

Mouse integration uses direct libSceMouse imports and retry/reopen logic. Pointer logic implements selection, word/line clicks, clipboard/context menu, wheel/autoscroll, input cursor positioning, on-screen keys and Settings controls. Speed is persisted under `/data/ps5term/mouse.speed`. `docs/HANDOFF.md` records hardware checks for basic virtual keyboard/mouse events, and lists remaining physical interaction checks. These are historical records, not fresh validation by this scan.

The renderer supplies native VideoOut and bitmap text/canvas drawing. There is no session/tab/pane manager or general terminal-engine abstraction yet. Research hooks are normally stubbed and enabled with `PS5TERM_RESEARCH`.

## Shell and utilities

`prepare-backend.py` copies the vendor to a generated build tree, selects the core bundle, binds the server to loopback 2324, adapts greetings and bounds, and uses `backend/ps5term_parse.h` for quoted/escaped single commands. Unquoted `|`, `&`, `;`, `<` and `>` are deliberately rejected. This is not a POSIX shell.

The core bundle has 43 command registrations, including `ls`, `rm`, `cp`, `mv`, `mkdir`, `cat`, `grep`, `find`, `stat`, `hexdump`, `id`, `ps`, `procstat`, `sysctl` and filesystem/credential-related tools. These are existing shsrv implementations, not BusyBox or a complete FreeBSD base. Shell-level builtins also exist. External ELF commands use upstream launch support. Normal local sessions use pipes; a native PTY has not been established.

SSH has its own pipe-backed shell adapter and PTY-request emulation in `third_party/ps5sshd/src/shell.c`; its `pty` variable does not mean it allocates a kernel PTY. The local single-command parser adaptation is not also applied to SSH's embedded shell by `prepare-ssh.py`. Their behavior must be reconciled before promising one consistent shell experience.

## SSH ownership and configuration

Pinned sources include ps5sshd 1.1.0, shsrv, wolfSSL 5.9.4 and wolfSSH 1.6.0. Preparation scripts preserve vendor sources and adapt generated copies. Dependencies have URL/hash checks and source notices.

Ownership remains incomplete: Settings' flag and mouse preferences are in `/data/ps5term`, but password/key/tool paths and vendor defaults still use `/data/ps5sshd`. Settings supports enable, retry and password reveal, not a complete SSH configuration editor. The code requests upstream root credentials; exact runtime privilege/capability behavior remains context-dependent.

Private saved evidence reports SSH verification passing on port 22. A separate saved disable observation reports the listener/session stayed open; conversation context says the user chose to keep SSH enabled, so this does not establish a failed disable action. Toggle-off behavior still needs an intentional, recorded test. Public README/UI statements that all SSH validation is pending are therefore too coarse.

## Build, packaging and tests

The native app pipeline compiles C/C++, links a PIE through public SDK stubs, converts it to Sony-style ELF, wraps a development FSELF and assembles the title folder. A generated mouse stub and LOAD alignment check address recorded launch failures. Optional FFPKG/FFPFSC tooling and PowerShell entrypoints are inherited from the template. PacBrew integration is build-time sysroot/static-library support, not a runtime package manager.

`make app` depends on SSH, shell and runtime generation. `make release` adds a deterministic ZIP and checksum. Catalog tooling prints a proposed ProsperoStore record; it does not publish. Generic FTP deployment remains, while the latest input handoff specifies the private no-launch deployment workflow and user-driven title launch.

Existing `dist/FAKE99260/eboot.bin` is 60,514 bytes, SHA-256 prefix `7fe1bb3beb8d210f`, matching the input handoff's recorded build identity. The current ZIP's eboot and both server assets match the folder; server assets also match `assets/backend`. This is artifact consistency, not a fresh source rebuild or console result. Older outputs remain alongside it and should not be mistaken for current releases.

`make test` runs renderer and Python tooling suites, but does not include `make test-terminal`. The latter builds terminal/Telnet, pointer, settings, SSH-service, parser and controller suites. The handoff records a Linux pass with Clang 18 and a macOS settings-test declaration mismatch; no tests were rerun during this scan. `make check` likewise omits the separate terminal suite. CI wiring and documentation need reconciliation.

## Firmware compatibility requirement

The user wants support across jailbroken PS5 firmware where feasible; 13.60 is the only available hardware test firmware. Do not equate a broad target with verified support.

Keep portable terminal/UI logic separate from Sony API adapters and privileged launch/credential code. Current risks include manually declared keyboard/pad/mouse structures, SceNet-specific nonblocking handling selected by `__FreeBSD__`, upstream kernel helpers, fixed sysctl process-record field offsets, loader/container assumptions and credential behavior. SDK pinning and successful compilation do not resolve these risks.

Prefer capability detection and graceful feature disablement. Where firmware-specific data is unavoidable, keep it in an explicit reviewed backend, reject unknown layouts and record per-feature compatibility: source-supported, reference-tested, hardware-tested or unknown. Preserve the core terminal when an optional service is unsupported. Fat and Pro hardware coverage must also be tracked separately.

## Gaps to resolve first

1. App-owned launch and privilege lifecycle without external 9021.
2. Native PTY/process/job-control capability evidence and consistent local/SSH shell behavior.
3. Full terminal engine and session boundaries, preserving the input implementation.
4. PS5Term-owned SSH paths, configuration and deliberate stop/resume validation.
5. Package manifests/recipes and a small verified tool baseline.
6. Wire terminal suites into the normal verification path; reconcile stale template/hardware docs.
7. Gate optional jails/VMs on separate capability/performance results.

The external Mac lab and kernel/library evidence plans are linked from the other research notes. No guest subsystem, jail manager, BusyBox port, runtime package manager or faithful PS5 emulator is implemented in the application tree.
