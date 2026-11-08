# SSH session-channel reuse

Continue the terminal compatibility roadmap with a bounded increment: sequential
native and Linux session channels may reuse one authenticated SSH transport.
Keep one live channel per transport; concurrent channels remain rejected. SFTP
retains its existing exclusive transport lifecycle because its library state is
transport-owned. Forwarding remains rejected. Do not claim full multiplexing.

1. Add failing interoperability tests for sequential exec status/stderr, closing
   an active shell channel, and independent per-session directories.
2. Separate authenticated transport lifetime from channel initialization and
   reset channel-owned buffers, PTY, command and guest handles after peer close.
   Preserve keys/authentication and clear service drain deadlines between sessions.
3. Run host SSH contracts, terminal regression checks and native SDK build.
   Deploy only after package and physical phat validation can be performed safely.

Retain bounded single-channel memory and ten-second close handshake timeout.
An idle authenticated connection remains subject to the existing service timeout.
No raw payloads, firmware changes or Pro console operations are involved.

## Completed validation

- Both new native reuse tests failed against the original server (transport closed).
- 14 native adapter contracts and 16 service contracts passed; those 30 also
  passed with ASAN/UBSAN instrumentation of the app-owned adapter.
- Eight SFTP contracts passed; SFTP remains an exclusive connection.
- OpenSSH ControlMaster slaves disable TCP fallback, so a fresh authentication
  cannot hide failed reuse. A command after eleven seconds verifies the service
  clears the previous channel's close deadline.
- Mac/Linux terminal suites and the native SDK app build passed.
- Phat 13.60 build `9b9d5e9af10d3c415113adf41beec79efe3f25e7e37a832f861b38d311e3a264`:
  twelve native commands, a command after the close deadline, closed PTY directory
  reset, three Linux exec sessions with preserved Alpine files, and three real
  OpenSSH ControlMaster commands all passed. Strict saved host key checks were used.
- GUI terminal integrations, overlapping channels and other firmware remain unverified.
