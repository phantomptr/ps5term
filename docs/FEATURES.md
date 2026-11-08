# Feature access and readiness

Every action works with a keyboard alone, a mouse alone (through the on-screen
keyboard and buttons), a keyboard and mouse, or a controller alone. On-screen hints
name the keys of the input used last: keyboard, mouse or controller.

| Action | Keyboard | Mouse | Controller |
|---|---|---|---|
| Enter commands | Type, Enter | Keyboard button, click keys, Run | Triangle, D-pad/Cross, R2 |
| Esc, Tab, Ctrl, Alt, arrows, page keys | The keys themselves | Last two rows of the on-screen keyboard | Last two rows; Ctrl and Alt apply to the next key, Shift (R3) to terminal keys |
| Function keys, Del, Ins in programs | F1-F12 (with Shift/Ctrl/Alt), Delete, Insert, while a Linux program runs | On-screen F1-F10, Del, Ins | On-screen F1-F10, Del, Ins |
| Settings | Ctrl+Shift+S; F6 in the built-in shell | Settings button | Options |
| Navigate Settings | Tab/Shift+Tab or Up/Down; Enter/Space | Click the control | D-pad Up/Down; Cross activates focused control |
| SSH on/off | Settings: focus Enable/disable SSH, Enter/Space | Toggle or Enable/disable SSH | Focus SSH, Cross |
| Show/hide password | F2 in Settings, or focus button | Show/hide password | Triangle in Settings |
| Change password | F3 in Settings, type, Enter, confirm | Change password, click keys, Next/Save | Square in Settings, on-screen keyboard |
| Cancel password edit | Esc | Cancel, or right-click | Circle |
| Mouse speed | Left/Right in Settings, or focus -/+ | -/+ buttons | D-pad Left/Right |
| Retry SSH server | F5 in Settings, or focus Retry SSH | Retry SSH button | Focus Retry SSH, Cross |
| Start/stop Linux | Settings: focus Start/Stop Linux, Enter/Space | Start/Stop Linux button | Focus Start/Stop Linux, Cross |
| Choose /mnt/ps5: /data or / | Settings: focus PS5 files, Enter/Space | PS5 files button | Focus PS5 files, Cross |
| Expand persistent storage | Settings: focus Expand, Enter | Expand button | Focus Expand, Cross (Linux stopped) |
| Open/install Alpine with apk | Settings: focus Open Alpine, Enter | Open Alpine button | Focus Open Alpine, Cross |
| Text size | Settings: focus Text, Enter | Text size button | Focus Text, Cross |
| Reconnect | F5 in the built-in shell, or once Linux has stopped | Reconnect button; click twice to restart a running Linux | Cross when offline |
| Interrupt command | Ctrl+C | Interrupt button | L2+Circle, or Ctrl then c |
| On-screen keyboard | Ctrl+Shift+K; F4 in the built-in shell | Keyboard button | Triangle |
| Select text | Ctrl+Shift+Space; arrows/PgUp/PgDn move, Space starts here, Enter copies, Esc cancels | Drag; double/triple-click; in programs that use the mouse, Select text from the menu, then drag | L3 → Select text; D-pad/L1/R1 move, Square starts here, Cross copies, Circle cancels |
| Copy/paste | Ctrl+Shift+C/V, Ctrl/Shift+Insert | Right-click menu; middle-click paste | L3 → Copy / Paste |
| Actions menu (Select all, Clear, tabs, split, find) | Shift+F10 or Menu key; arrows, Enter; unavailable items are skipped | Right-click (outside the output in programs that use the mouse) | L3, D-pad, Cross |
| New / Close tab | Ctrl+Shift+T / Ctrl+Shift+W | New tab / Close buttons | L3 → New tab / Close tab |
| Split / unsplit | Ctrl+Shift+E | Split / Unsplit button | L3 → Split / unsplit |
| Focus other pane | Ctrl+Shift+O | Focus other button, or click its output | L3 → Focus other pane |
| Resize split panes | Ctrl+Shift+Left / Right | Drag the divider between panes | L3 → Divider left / Divider right |
| Switch tabs | Ctrl+Shift+1-4, or Ctrl+Page Up / Down (programs get these with one tab) | Click a tab | L3 → Previous tab / Next tab |
| Find text in retained output | Ctrl+Shift+F; type query; Enter/Down next, Up previous, F5 refresh, Esc close | Right-click → Find text; Previous/Next/Refresh/Close buttons | L3 → Find text; Triangle types; L1/R1 cycle; Circle closes |
| Scroll output | Page Up/Down in the built-in shell; Shift+Page Up/Down in Linux | Wheel; Shift+wheel for pages | D-pad |
| Mouse in programs | - | Programs that ask get the mouse; hold Shift, or use Select text, to select | - |
| Stop Linux, Expand storage (confirm) | Press again within 5 s | Click again | Cross again |
| Close Settings | Esc or focus Back, Enter | Back or right-click | Circle |
| About (credits, version, disclaimer) | F1 in Settings, or focus About PS5Term; Esc back | About PS5Term; Back or right-click | Focus About PS5Term, Cross; Circle back |

Tab and Shift+Tab are terminal input in Linux when no app dialog/menu is open (the
built-in shell has no completion). Settings focus follows the screen top to bottom. App
menus own input while open; Esc returns to the terminal. Password entry does
not send typed secrets to the terminal or diagnostics. Clipboard is app-local.

SSH is the app-owned server with editable password and public-key commands. Log in
as `ps5term` for the built-in native shell and SFTP of the PS5's files, or as `linux`
for a real Linux shell with its own PTY. Up to four SSH connections run at once, each with up to four session channels. The built-in
shell is not a complete POSIX shell; use `help` to see its commands. Public-key
management is through SSH commands, not a Settings key editor.

The experimental Linux subsystem starts from Settings → Start Linux or from a `linux`
SSH login. Stop Linux returns to the native console, after flushing and remounting its
disks. `/mnt/data` persists; root-system changes are discarded. `/mnt/ps5` shows the PS5's
`/data` by default, or everything the app can see if `/` is chosen in Settings.
Outbound IPv4 works through the app, and nothing can connect in. See
[subsystem controls and limitations](SUBSYSTEM.md). Native jails and hardware
virtualization are research directions.

Controller, Remote Play and SSH paths were exercised on a phat console at firmware
13.60. Physical USB keyboard and mouse handling is covered by host regression tests
only; a host test is not proof of PS5 device behavior.

Text size cycles small, normal, and large and is saved between launches. In controller
text selection, Square resets the anchor, L1/R1 page, and Circle cancels. Clipboard
contents stay inside PS5Term. See [persistent Alpine and packages](SUBSYSTEM.md#persistent-alpine-and-packages).

Find text performs a case-sensitive literal search within retained physical rows,
including UTF-8 pasted from the app clipboard. It does not join wrapped rows.
Results refresh when the query changes or Refresh is activated; continuous output
does not restart the search. Scanning is incremental, with at most 2048 matches
and a 96-byte query. A + after the count means additional matches were omitted.
The selected match is highlighted. Search owns input and preserves the shell draft.

Up to four tabs have separate screens, scrollback, drafts and shell sessions.
New tab uses the active tab's shell type: a native built-in shell, or an independent
Linux PTY in the existing guest. Linux tabs start in the base guest; run
`ps5term-alpine open` to enter the installed Alpine environment. The four guest-agent
PTY slots are shared with SSH; exhaustion is reported without closing another shell.
Inactive tabs continue processing bounded output. Closing a tab releases only its
shell. The primary tab cannot be closed and owns the shared Linux VM; Settings
start/stop actions return to it. Stopping Linux ends all its PTYs, including SSH.
Session tabs and commands are not restored after closing the app.

Split shows two sessions side by side, creating a second tab if needed. Each shell
receives its actual pane size. The highlighted pane owns typing, search and text
selection; clicking the other pane focuses it without sending the click to the
program. Wheel scrolling focuses the pane under the pointer. Selecting a hidden tab
replaces the focused pane. Unsplit keeps both sessions alive. Closing an extra tab
while split returns to its surviving neighbor at full width. The divider can be moved
by dragging it, with Ctrl+Shift+Left/Right, or from the L3 menu; when the screen is not
split, Ctrl+Shift+Left/Right go to the program as usual. Only two side-by-side panes
are supported.

The native shell includes `du [-shk] [path]` for one allocated-storage summary,
without starting Linux. It defaults to the current directory and KiB; `-h` formats
human-readable units. Hidden entries count; symlinks are not traversed, hard-linked
files count once, and sparse files use their allocated blocks. Scans yield between
small batches and Ctrl+C cancels them. 32 directory levels, 100000 visited entries and 2048
hard-link identities are fixed limits. A filesystem permission/metadata error or
limit failure reports an error and no complete summary; the native firmware may
restrict link metadata. Use the Linux `du` for its full options and multiple operands.

Native filename expansion supports `*` and `?` in the final component, for example
`ls /data/*.log`. Matches sort by byte value; spaces and shell operators in names
stay within one argument. Quotes and backslash escapes protect literal characters;
unquoted variable values can supply patterns. Dotfiles require a leading dot in
the pattern. Unmatched patterns stay literal. Matching is byte-oriented, so `?`
matches one byte, not a full Unicode character.

Wildcard directory components, bracket expressions and unquoted patterns in
redirection targets are rejected before a pipeline runs or opens its redirections.
Use Linux for full shell expansion. Native limits are 31 arguments per pipeline
stage, 32 KiB of expanded argument storage and 100000 directory entries per pipeline.
Scans process at most 32 work units per tick and can be interrupted.

System commands in the native shell:

- `ps [-f]` lists processes. The console shows an app only its own process, so on
  13.60 `ps` lists PS5Term and says so. Each listing checks the app's own record
  against its real pid, parent and user; if a different firmware changes the record
  layout, `ps` falls back to PID and name and says why.
- `kill [-SIG | -s SIG] PID...` and `kill -l`. Signal 0 checks a PID without sending
  anything. PID 1 and PS5Term itself are refused; close the app from the Home screen.
- `uptime`, `nproc`, and `sysinfo` (model, CPUs, firmware SDK version, kernel build,
  uptime, app pid/uid, and what the app may see).
- `free [-h]`. The PS5 kernel has no memory-total sysctls, so it prints PS5Term's own
  memory use and says console totals are not available.
- `sysctl [-n] NAME...` reads kernel values: text, numbers, or the first 64 bytes of
  a structure in hex. Setting values is not supported.

`df` is not available in the native shell because the app may not query filesystem
sizes; use `df` in Linux for its own disks.
