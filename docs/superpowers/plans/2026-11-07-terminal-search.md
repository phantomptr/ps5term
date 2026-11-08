# Terminal search increment

Use a bounded incremental literal UTF-8 matcher over retained physical lines and
visible terminal cells. At most 4096 cells per tick; 2048 results; 96 query bytes.
No subprocess, unbounded scan, or new worker. Render the selected match and expose
previous/next/close controls through keyboard, mouse and controller OSK. Search
owns all typing and mouse reporting while open. Close on reset, resize or screen
switch; retain the user's shell input. Snapshot search refreshes on query edits
or explicit Refresh, avoiding restart starvation from continuous output.

1. Regression tests for Unicode, overlap, history, budgeting, truncation and reset.
2. Add engine and app controls; regression input ownership for all input paths.
3. Focused host tests, Linux suite, native package and guarded phat acceptance.

Session tabs/panes require independent terminal/PTY ownership and remain the next
increment, rather than relabeling one shared session as multiple tabs.

Verified 2026-11-07: macOS terminal suite, Linux make test, fresh ASan/UBSan
search tests and native build passed. Guarded phat deployment/readback and normal
Home launch passed (build d0148bc46b68, PID243). Controller search over running
Linux found and highlighted two PS5TERM matches, cycled with R1/R2, and closed
without changing the Alpine prompt. Keyboard/mouse ownership has host coverage;
physical USB acceptance remains pending. Existing apk/Bash and persistent files
survived. Evidence: .local/dev/claude-20261104/hw/search-20261107-release/.
Our RP8 and builder were stopped, leaving the app and SSH running. Preserve the
known-good backup and guest assets. Independent session tabs/panes remain next.
