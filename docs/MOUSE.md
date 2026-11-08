# Mouse

PS5Term works with a mouse: a USB mouse on the PS5, or a virtual mouse such as
ps5upload's Remote Play keyboard & mouse mode. The controller and the keyboard
work as before.

## What it does

- **Pointer**: drawn on top of everything and moved by the mouse. It stays on
  screen and appears when the mouse moves or a button is pressed. It fades
  after 3 s without mouse input, and hides when you use the controller or
  keyboard while the mouse is still. The header shows `MOUSE` while one is
  connected.
- **Terminal output**
  - Drag to select text.
  - Double-click selects a word. Triple-click selects a line.
  - The wheel scrolls 3 lines per notch, or a page with Shift held.
  - Dragging past the top or bottom edge scrolls the output.
- **Input line**: click to move the text cursor.
- **Right-click menu**: Copy, Paste, Select all, Clear. Esc, Circle, or a
  click outside the menu closes it.
- **Middle-click** pastes.
- **Clipboard**: PS5Term keeps its own clipboard. From the keyboard, Ctrl+Shift+C
  or Ctrl+Insert copies, and Ctrl+Shift+V or Shift+Insert pastes. Plain Ctrl+C
  still interrupts. A paste goes into the input line with newlines turned into
  spaces. Nothing runs until you press Enter.
- **Esc** drops a selection first. After that, Esc behaves as before.
- **On-screen keyboard** (Triangle): click a key to type it. The keys and the
  Shift, Delete and Run labels highlight under the pointer.
- **Settings**: click `Options / Settings` to open. The SSH switch and each
  help line are buttons. A right-click goes back, like Circle. **Mouse speed**
  ranges from 0.5x to 3x (default 1x). Change it with the - and + buttons, or
  with Left/Right on the D-pad or arrow keys. It is saved in
  `/data/ps5term/mouse.speed`.

## How it is built

- `src/mouse.cpp` links libSceMouse directly, the same way input.cpp links the
  keyboard. It loads sysmodule `0x00a9`, then calls `sceMouseInit`. It calls
  `sceMouseOpen(user, 0, 0)` again every 2 s until the open succeeds; the first
  open can fail just after launch. It reopens the mouse once after a
  disconnect, when the user changes, and after a read error.
- The pinned SDK has no libSceMouse stub. `tools/build.sh` generates
  `build/stubs/libSceMouse.so` on every build with `tools/make-stub.sh`.
  Compared with earlier builds, the only new imports are
  sceMouseInit/Open/Read/Close and `libSceMouse.sprx`.
- `src/pointer.cpp` holds the pure logic: hit-testing, selection, wheel, click
  counting and the speed preference. It is covered by
  `tests/terminal/test_pointer.cpp`, and mouse flows through `App` are covered
  in `test_controller.cpp`. Run them with `make test-terminal`.
- Diagnostics: if `/data/ps5term/mouse.debug` exists at start,
  `/data/ps5term/mouse.log` records the open result, connects and disconnects,
  clicks and actions. The log is capped at 4000 lines.

## Build check: RELRO mapping

The first mouse build failed with CE-107750-0 because the original native converter
used `.got`'s file offset as the RELRO origin even when `.data.rel.ro` preceded it.
Constant pointer tables exposed this bug; avoiding them was the initial workaround.

The converter now derives the RELRO file origin from the GOT's address delta and
rejects inconsistent section mappings. `test_native_layout.cpp` covers pointer
prefixes, preserved bytes and process-parameter mapping. A build including
libvterm's pointer tables launched and rendered on the phat on 2026-11-01.
Pointer tables no longer require that workaround. `tools/build.sh` still rejects
LOAD segments whose file offsets and addresses disagree modulo their alignment.
