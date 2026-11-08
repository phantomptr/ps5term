# SSH terminal compatibility

The compatibility target is standard SSH2, shell/exec channels, terminal input,
resize, Unicode and SFTP across Warp, Ghostty, iTerm2, Terminal.app, Windows
Terminal, PuTTY, Kitty, WezTerm and Alacritty. A terminal application's optional
remote extension is a separate integration and must be tested independently.

## Evidence today

The owned server's loopback service passes actual OpenSSH password authentication
and exec with `TERM=xterm-256color`, `xterm-ghostty`, `screen-256color` and
`tmux-256color`. Paramiko additionally exercises streaming, independent sessions,
errors/exit status, reconnect, persisted identity and service stop. These tests run
on macOS and Linux. They do not constitute tests inside each named GUI terminal,
or proof of a complete shell/PTY running on PS5.

The experimental owned PS5 build uses login `ps5term`, port 2222 and identity
storage `/data/ps5term/ssh`. The legacy/default build uses the credentials described
in SSH.md. Do not silently substitute one identity, permission context or port for
another. The owned server runs with the app's permissions, not a promised root UID.

Native phat 13.60 tests also pass authenticated commands, real OpenSSH exec with
these TERM values, a non-PTY shell, Settings disable/disconnect, and app close/reopen
with the same host key and password. The app runs as UID/EUID 1. Each connection
supports up to four concurrent native/Linux session channels, plus sequential
reuse of closed slots. Host tests cover independent shell directories, a stalled
reader alongside other commands, exact per-channel exit statuses, rekeying,
Linux resize/EOF routing, and OpenSSH ControlMaster with an existing shell.
SFTP requires its own exclusive connection. These protocol checks do not establish
GUI-client integration compatibility. See OWNED_SSH.md for close/idle deadlines
and the guest's shared terminal-slot limit.

## Connection baseline

Use the `linux` login for anything that expects a real terminal or shell. It runs a
POSIX shell on a real PTY in the guest, with resize, signals and terminfo for xterm,
screen and tmux; other `TERM` values are treated as `xterm-256color`:

```sh
ssh -p 2222 linux@192.168.86.99
```

For the PS5's own files, use the unwrapped SSH executable and no PTY:

```sh
command ssh -T -p 2222 ps5term@192.168.86.99
```

The built-in interpreter is not POSIX sh/Bash; remote integration scripts containing
`if`, functions, expansions or command substitutions cannot work there.

- **Warp:** tested 2026-11-07 with Warp 0.2026.10.21 by running its own SSH wrapper
  (`warp_ssh_helper`, taken from the installed app) against the phat (13.60). The
  wrapper sends a 5-6 KiB setup command with `-t`. The `linux` login runs it on a real
  terminal; Linux's login shell is `sh`, so Warp treats it as a plain shell. The
  `ps5term` login cannot run a shell script, so Warp's setup command opens the PS5 shell
  directly, which is what the script itself does for shells it does not know. Before
  this, both logins failed with `exec request failed`: commands were limited to 512
  bytes and ran without a terminal. A real Warp window `linux` login was then confirmed
  on the phat.
- **Ghostty:** tested 2026-11-07 with Ghostty 1.3.2 and its `ssh-env` and
  `ssh-terminfo` features through `ghostty +ssh` against the phat (13.60). The `linux`
  login installs Ghostty's terminfo, opens a real terminal (`TERM=xterm-256color`,
  256 colours, resize) and caches the result. The `ps5term` login cannot install
  terminfo; Ghostty prints one warning and continues normally. The guest treats
  `xterm-ghostty` as `xterm-256color` on purpose, because Linux's home folder resets
  when it stops. Note: `ghostty +ssh host 'command'` appends Ghostty's install script to
  your command; use it for interactive logins. Before 2026-11-07, a `linux` command
  that exited without reading the input it was sent (as that appended script can be)
  stopped the Linux SSH link until Linux was restarted. The guest agent now ends only
  that command's session.
- **iTerm2:** plain SSH and optional shell/SSH integration are distinct. Its
  integration scripts need a compatible shell. Proprietary escape sequences and
  file-transfer features need their own tests; protocol-level OpenSSH results do
  not prove them.
- **Other terminals:** clients using OpenSSH share the baseline protocol target.
  PuTTY and platform-specific client behavior require actual interoperability
  checks. No full-screen, clipboard or terminal integration is claimed yet.

## Acceptance checklist

- [x] Real OpenSSH authenticated exec and exit status on both development hosts.
- [x] Above TERM values do not break non-PTY exec.
- [x] Host identity persists, and service stop disconnects active sessions.
- [x] Owned PS5 authenticated shell/exec, stop and reopen (phat 13.60).
- [x] Real terminal allocation, resize, Ctrl+C and exit status with the `linux` login
  on phat 13.60, using Paramiko. Interactive OpenSSH and UTF-8 still need a manual pass.
- [x] Complete shell with pipes, redirection, scripts, external commands and jobs, in
  the Linux guest. The native `ps5term` shell stays limited.
- [x] Public-key authentication on development hosts (Ed25519, ECDSA P-256/P-384).
- [x] Native phat 13.60 key enrollment, OpenSSH login and revocation.
- [x] SFTP on development hosts and native phat 13.60 (including OpenSSH).
- [x] Ghostty 1.3.2 SSH wrapper (`ssh-env`, `ssh-terminfo`) with both logins on phat 13.60.
- [x] Warp 0.2026.10.21 SSH wrapper with both logins on phat 13.60 (no GUI window).
- [x] Warp window session (Warp 0.2026.10.21, `linux` login) on phat 13.60, by the user, 2026-11-07.
- [ ] Manual sessions in iTerm2, with precise versions recorded.
- [ ] Optional Warp/Ghostty/iTerm2 integrations tested individually.

Primary references:
[Ghostty SSH](https://ghostty.org/docs/features/ssh),
[Ghostty shell integration](https://ghostty.org/docs/features/shell-integration),
[iTerm2 shell integration](https://iterm2.com/documentation-shell-integration.html),
[Warp SSH requirements](https://docs.warp.dev/terminal/warpify/ssh/),
[Warp legacy SSH](https://docs.warp.dev/terminal/warpify/ssh-legacy).
