# Split panes implementation plan

User delegated design and implementation; no approval or new checkout is needed.

Design: show two existing tab sessions side by side. Split uses the next existing
session, or creates one if only the primary exists. A fixed left/right pair retains
its positions when focused. Selecting a hidden tab replaces the focused pane.
Unsplit keeps the focused shell and restores full width. Closing an extra tab in
split view collapses to its surviving neighbor; the primary remains protected.
Both models and PTYs receive their real pane width, including background output.
Only the focused pane receives input, selections, search and terminal mouse events.
Clicking an inactive pane focuses it and consumes that click. Wheel targets the
pane under the pointer; held mouse reports are released before changing focus.
OSK covers lower rows without resizing PTYs, preserving current behavior.

Controls: Ctrl+Shift+E split/unsplit, Ctrl+Shift+O focus other; L3 menu entries;
mouse buttons in the header and click-to-focus. The existing four-session limit,
shared VM and SSH guest-PTY pool remain. No recursive split tree in this increment.

- [x] Regression tests: isolation, geometry, focus, closing, hidden tabs, OSK,
      search ownership and click-to-focus. Verify failing before code.
- [x] Implement bounded pane state and all-session resize in app.hpp/app.cpp;
      common output renderer accepts session model, viewport and focus.
- [x] Add input shortcuts and controller/mouse actions in input/pointer files;
      update docs/FEATURES.md.
- [x] Host suites, Linux suite, focused sanitizers and native build/package.
- [x] Guarded phat-only stop/update/observed launch through PS5Upload; physical
      split/focus/command/close and SSH/persistence acceptance. Preserve rollback.
- [x] Stop owned test stream/builder and checkpoint actual evidence/remaining work.

Validation: full Mac and Linux terminal suites, focused ASAN/UBSAN controller
and app VT tests, native build and package passed. Physical phat acceptance
verified separate native output, both Linux PTY sizes 35x117, full-width 36x240,
controller focus, collapse on close, a surviving SSH PTY and persisted Alpine
packages/files. Private evidence: panes-20261107-release/acceptance.json.
Cleanup is recorded separately in the current task checkpoint.
