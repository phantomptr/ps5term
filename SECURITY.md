# Security Policy

## Supported versions

Only the **latest release** receives fixes. Please reproduce an issue on the newest
[release](https://github.com/phantomptr/ps5term/releases) before reporting.

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Report privately with GitHub's
**[Report a vulnerability](https://github.com/phantomptr/ps5term/security/advisories/new)**
button (Security tab → Report a vulnerability). Only you and the maintainer can see it.
If you can't use that, ask for a private channel on [Discord](https://discord.gg/fzK3xddtrM);
don't post details publicly.

Please include what the issue is and its impact, steps to reproduce, and the PS5Term
version and firmware. This is a free, volunteer-maintained project, so timelines are
best-effort, but security reports are prioritised.

## Scope

PS5Term runs on a jailbroken PS5 and trusts the local network it is on.

- **SSH and SFTP (port 2222, off by default).** Logins use the password shown in
  Settings or keys in `/data/ps5term/ssh/authorized_keys`. A way to log in, run commands
  or reach files without those, to escape the per-connection limits, or to crash the app
  from the network is in scope.
- **The Linux guest.** It can reach the internet through the app's NAT but nothing can
  connect in to it. A way for a program inside Linux to reach the PS5 beyond the shared
  folder chosen in Settings (`/mnt/ps5`) is in scope.
- **Out of scope:** anything that needs the SSH password or physical access to the
  console, the choice to share `/` with Linux (Settings warns about it), and issues in
  the jailbreak, loader or other payloads.
