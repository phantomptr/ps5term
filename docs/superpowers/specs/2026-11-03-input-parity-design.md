# Input parity

All existing app actions must be reachable without a controller. Controller
shortcuts remain, with plain action labels on clickable buttons. Settings has
visible focus, Tab/Shift+Tab and Up/Down navigation, Enter/Space activation and
Escape to close. Password editing preserves ordinary typing, with Tab navigation
between text and Shift/Delete/Next/Cancel controls. Input never leaks behind a
modal. A terminal toolbar exposes keyboard, reconnect and interrupt to mouse
users. F4 toggles the on-screen keyboard, F5 reconnects, F6 opens Settings;
Menu/Shift+F10 opens a keyboard-navigable action menu. Tab stays terminal input
outside app dialogs. No per-device mode or automatic input-device switching.

Alternatives considered: shortcuts alone are undiscoverable; separate device
screens duplicate behavior. Shared actions with focus and pointer activation
provide consistent access while preserving existing controller shortcuts.
Prior user instruction delegates design decisions and opts out of questions.

Subsystem remains unavailable in the installed build: host boot proof is not
native app integration. Record this explicitly rather than drawing a toggle
that cannot safely start it. SSH service/password/speed controls are in scope;
new guest packaging and physical acceptance remain a separate milestone.

Verify regression tests for modal isolation, forward/reverse focus, keyboard
menu selection, mouse toolbar, password actions, and preserved terminal input.
