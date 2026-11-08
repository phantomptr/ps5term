# Independent session tabs

Decision under the user's delegated design authority: four visible tabs maximum.
Keep one Linux VM and its persistent disk owner. Each additional Linux tab borrows
one independent guest-agent PTY; native tabs own independent built-in connections.
Do not duplicate a VM, attach two views to one input stream, or interfere with SSH
sessions. Guest-agent capacity is shared: report exhaustion without evicting peers.

Each tab owns its model, scrollback, editor/history, transport and scroll position.
Pump inactive tabs with bounded work so long-running output cannot block the active
view. Close releases only that shell; the primary tab owns Linux and cannot be
closed. Global Settings controls the primary VM, explicitly returning to its view
for start/stop/restart. Switching tabs cancels selection/search/OSK, retaining drafts.

Expose visible tab buttons plus New/Close, Ctrl+Shift+T/W and Ctrl+PageUp/PageDown,
and controller L3 actions. Keep existing keyboard/program shortcuts intact.

1. Failing regression tests: independent native cwd/drafts/output, close and limit;
   borrowed Linux PTY closure/transport independent of shared VM/other sessions.
2. Implement connection borrowing, lazy bounded tab ownership and all input paths.
3. Focused tests, full host checks, native build/package, guarded phat acceptance.
4. Preserve rollback, release our RP stream and builder, write checkpoint.

Split panes remain a separate layout increment after tab ownership works. Do not
claim full terminal-client, USB, all firmware or public release acceptance.
