# Terminal engine foundation implementation plan

> Execute directly with superpowers:executing-plans. The user authorized autonomous implementation and no questions. No subagents, commits, or pushes.

**Goal:** Add the spec's pinned libvterm engine as a tested portable cell model, without replacing the other agent's input/rendering integration.

**Architecture:** `TerminalModel` owns one libvterm instance, exposes Unicode cells/cursor/alternate-screen state, and bounds dimensions, incoming work and generated terminal replies. Keep legacy `Screen` in use until the new renderer and session integration have a separate complete test cycle.

**Tech stack:** libvterm 0.3.3 (MIT), C11/C++20, existing Mac/Linux terminal suites and native compiler.

**Spec:** `docs/superpowers/specs/2026-11-01-ps5term-design.md`, section 4 / M2. This is M2's engine foundation only; font rasterization, scrollback/search, tabs and live integration follow.

## Constraints and review focus

- Preserve all controller, keyboard and mouse paths; no new external payload dependency.
- Phat `.99` only for any hardware work. Engine tests are offline.
- Vendor only the official pinned source and license, record its archive SHA-256 and included-file hashes. No unverified moving branch.
- Fragmented UTF-8/escape sequences must match unfragmented transcripts; wide and combining codepoints retain cell semantics.
- Resize rejects invalid dimensions before allocation; alternate-screen restoration preserves main cells.
- Generated replies and key/paste bytes share a bounded queue; explicit failure on overflow, no silently successful truncation.
- Cursor/attributes use libvterm semantics; no claim of matching rendered glyphs or a functioning native PTY.

## Task 1: Portable engine and source ownership

Files: `third_party/libvterm/{include,src,LICENSE,source.json}`, `src/terminal_model.hpp/.cpp`, `tests/terminal/test_terminal_model.cpp`, `tools/test-terminal.sh`, `tools/build.sh`, `NOTICE.md`.

Interface: `TerminalModel(rows,columns)`; `feed(string_view)->size_t` consumes at most 64 KiB; `cell(row,col)->Cell` includes six Unicode codepoints, width, RGB and styles; `cursor()->Cursor`; `resize(rows,cols)->bool`; `alternate_screen()->bool`; `key(Key,Modifiers)->bool`, `text(codepoint,Modifiers)->bool`, `paste(string_view)->bool`; `take_output(span<char>)->size_t`; `healthy()->bool`. Dimensions 1..120 rows,1..240 columns. Queue 256 KiB. Invalid construction is inert; invalid resize leaves old state. Output overflow latches failure; queued bytes remain readable. `Key` is model-local, not the existing input router enum.

- [x] Pin official 0.3.3 archive digest, preserve MIT license and source file hashes.
- [x] Write transcript tests and confirm missing model fails to compile: fragmented UTF-8/CSI; CJK/combining widths; cursor and truecolor/style; alternate screen; resize; cursor-position response; application-cursor keys; bracketed paste; invalid bounds; output overflow.
- [x] Implement model and compile upstream C independently of strict project C++ warning flags.
- [x] Run focused Mac/Linux tests, wire into all terminal suites, compile native sources; distinguish compile evidence from runtime support.
- [x] Record limitations and next integration boundary. Do not deploy a build solely to exercise an unused model.

## Task 2: Bounded scrollback snapshots

Add a portable `Scrollback` store and connect only the new model's libvterm
callbacks. Preserve Unicode cells and resolved RGB colors, retain original line
width, cap at 10,000 lines / 8 MiB of requested storage including line metadata,
and evict oldest whole lines. IDs increase across eviction/clear; expired IDs
return no cells. Allocation failure is explicit and never returns null into
libvterm. Main-screen scrolling records history; alternate-screen output does
not. CSI 3 J clears history. No legacy UI/input changes. Resize retains historical
snapshots at their original width; restoring history into enlarged screens and
logical-line reflow are separate integration work.

- [x] Test line/count/byte eviction, stable IDs/clear, width/style preservation,
      bounds and invalid budgets before implementing the store.
- [x] Connect main-screen callbacks and test alternate-screen isolation and clear.
- [x] Run focused Mac/Linux/sanitizer and native checks; keep live claims separate.


## Verified result and current boundary

The engine, allocation boundary, damage/cursor tracking, function keys, opt-in
mouse/focus reporting, and scrollback snapshots are implemented. Full Linux tests
pass 41 Python / 3 renderer / 16 terminal suites; macOS passes 16 terminal suites.
The model and scrollback pass AddressSanitizer/UndefinedBehaviorSanitizer on
macOS and a FreeBSD amd64 reference VM run. Native folder build validates;
none of these establish PS5 runtime VT correctness because the UI still uses
legacy `Screen`. No input/UI files were changed. The installed phat app remains
the previously verified build, not the latest unused-engine artifact.

`terminal_memory.c` retains all libvterm allocations under a 16 MiB hard cap.
Construction/feed/resize allocation failure escapes within C before returning
null to upstream, frees the partial graph and leaves the model inert. x86 uses
compiler jump builtins with no console setjmp import; ARM reference hosts use
standard C runtime jumps. Only the pinned engine's allocation sites and
allocation-free callbacks satisfy this boundary; audit it again on upgrades.
Output queue and retained snapshots remain independently readable after a model
fault. A new model is the recovery operation. Scrollback caps requested storage,
including line metadata, at 8 MiB / 10,000 lines; host allocator overhead is
additional and bounded by that line count.

Next integration work: font/glyph renderer, logical text selection/search and
resize reflow, session transport integration and live UI/input routing. History
currently stores physical-line snapshots; growing a screen does not pull history
back into it. Native child launch and PTY gates remain unresolved. Do not claim
standalone, a functioning PTY, complete Unicode glyph coverage, or new model
hardware validation from these tests.
