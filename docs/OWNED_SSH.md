# App-owned SSH and SFTP

The experimental owned service uses wolfSSH 1.6.0 and wolfCrypt from wolfSSL
5.9.4 directly. It builds into PS5Term, stores its own identity under
`/data/ps5term/ssh`, and uses login `ps5term` on port 2222. It does not build,
launch or import runtime state from ps5sshd/shsrv and has no payload-loader calls.
This is the default app build. Explicit legacy comparison builds are documented
in [SSH.md](SSH.md).

The owned profile offers the standard hybrid key exchange
`mlkem768x25519-sha256`, using upstream wolfCrypt's portable C ML-KEM-768,
SHA-3/SHAKE and X25519. OpenSSH 9.9+ can negotiate it; classical algorithms remain
available for older clients. No client warning suppression is required. The
existing ECDSA host identity and password/public-key authentication are separate
from this change. Forced and default hybrid negotiation were verified with
OpenSSH 10.5 on phat firmware 13.60, including the previously trusted host key.
Loopback tests additionally authenticate and execute a command over hybrid KEX,
and verify classical-client fallback. This does not claim post-quantum host
signatures or authentication.

## Logins

```sh
ssh -p 2222 linux@192.168.86.99            # A real Linux shell with its own PTY.
ssh -p 2222 linux@192.168.86.99 'uname -a' # A command, with exact bytes and exit status.
command ssh -T -p 2222 ps5term@192.168.86.99  # The built-in native shell.
```

Both names use the same password and keys; the name only selects where the session
runs. A `linux` login starts the guest if it is not running and waits for it to boot,
which switches the console's screen to Linux. Up to four sessions run at once, each with
its own PTY and size. Interactive sessions are exempt from the five-minute idle limit.

The built-in interpreter supports file and text commands, independent logical working
directories, pipes, redirects, `;`, `&&`, `||`, bounded streaming, stdout/stderr and exec
exit status. It is not a complete POSIX shell: no child processes, job control or
scripts. A `ps5term` client that requests a PTY gets correct line endings from this same
line shell, not a real terminal, so full-screen programs need the `linux` login.
Forwarding, environment and unknown subsystem requests are rejected or ignored. Each
connection permits up to four concurrent native or Linux shell/exec channels,
with independent directories, streams, PTY dimensions, EOF and exit status.
OpenSSH ControlMaster can run commands alongside an existing shell. Linux commands may
be up to 16 KiB and run on a real terminal when the client asks for one (`ssh -t`);
native commands are limited to 512 bytes, except Warp's SSH setup command, which opens
the interactive PS5 shell. Native commands do not read input sent over SSH (for example
`ssh ps5term@ps5 'cat > f' < file`); use SFTP or the `linux` login for that. Closed channel
slots can be reused. SFTP needs an exclusive connection and cannot follow shell/exec
or share a transport with another channel. Forwarding stays disabled.

Each channel must request a session within 15 seconds; idle non-terminal transports
expire after five minutes. If a closing channel's reader stays stalled for ten seconds,
that channel's unsent output is dropped and only that channel closes; sibling channels
keep running. A peer that also ignores the close for another ten seconds loses the
connection.
Linux channels also share the guest's four terminal slots with local tabs; a busy
guest can make an additional channel wait for a slot (up to two minutes).

See [terminal client compatibility](TERMINAL_CLIENTS.md) before enabling Warp,
Ghostty or iTerm2 remote extensions. OpenSSH protocol tests do not establish
compatibility with every GUI client or its optional integration.

## Lifecycle and identity

`backend/owned_ssh/service.*` owns at most four nonblocking clients, 60 seconds from
connecting to finish logging in, a five-minute IO idle deadline for non-terminal sessions, a
ten-second limit for a client ignoring close, and complete socket teardown. When all
slots are full, the oldest connection that has not logged in is replaced. Each
connection allows three wrong passwords. Failures delay the next handshake from the same
address only: one second per failure at first, growing with repeated failed connections to
at most 30 seconds, and forgotten after ten quiet minutes. Other addresses are not delayed. Each client gets a 256 KiB transport IO budget per service tick. The UI owner calls
the service; moving it to a separate worker requires measured need and bounded
shutdown. Disabling SSH or exiting the app closes all owned connections.

The service generates an ECDSA P-256 host key and a 192-bit random password.
Settings reveals the password until hidden again. Options → Square (USB: F3)
opens Change password. Enter a new password and confirmation, then use R2
(USB: Enter) to save. The controller keyboard is always available; mouse users
can click its keys and Next/Save. Circle/Esc cancels without changing the password.
Passwords accept 8–64 printable ASCII characters, including spaces. The entries
are masked and kept separate from command history, clipboard and raw keyboard
diagnostics; temporary input is wiped on save/cancel. Password changes also work
while SSH is disabled and do not enable the listener. A successful atomic save
closes every owned SSH/SFTP session, takes effect on the next login, and preserves
the host key and authorized public keys. A failed save keeps the existing password.
A persistent Settings message confirms success; passwords are never put in a toast. Private files are bounded,
regular, mode 0600, owned by root or the effective app UID; the directory is 0700.
Unsafe or incomplete identities fail startup instead of rotating silently. An update
file left by a crash is replaced, and a first run interrupted after writing the host
key keeps that key and creates only the password.
Native entropy comes from `/dev/random`, opened nonblocking without following
links and checked as a character device. There is no weak fallback.

Native phat 13.60 reports root ownership and zero links for these private files.
The native profile accepts zero as unavailable link metadata, or one, and rejects
higher counts; hosts require exactly one. Zero does not prove a single link.
The app runs as UID/EUID 1 on the tested console, with its actual app permissions.
A login name does not grant root or establish support on another firmware.

Native tests pass password authentication, real OpenSSH exec and non-PTY shell,
Settings disable/disconnect, and app close/reopen with the same host identity.
Private evidence is under `.local/dev/owned-native-*-evidence.json`, excluded from
releases. The legacy port 22 listener was left unchanged.

## SFTP build and bounds

Enable the native SFTP candidate with:

```sh
APP_DEFINITIONS='PS5TERM_OWNED_SSH PS5TERM_OWNED_SFTP' bash tools/build.sh Folder
sftp -P 2222 ps5term@192.168.86.99
```

The owned distribution omits unused legacy SSH/shell payloads and packages these
connection instructions. It reuses upstream `wolfsftp.c`, with reproducible patches applied to verified
source archives. Directory replies contain at most 32 records and 64 KiB;
allocation and encoding failures release the partial page. Each connection permits
four file handles and four directory handles. Transfers use 32 KiB read/write
payloads and bounded inbound packets; a larger read request gets a short read. Paths
up to 1 KiB work. Changing a file's owner is refused, because the SDK's call for it
would end the app. Disconnect frees open handles. Native
directories use 64 KiB buffers and the existing public-kernel dirent decoder.
Working directories are per connection; no process-wide chdir is used.

The PS5 SDK's lstat path fails in the tested app context. The native adapter first
tries lstat, then obtains metadata for readable regular files/directories using
no-follow open and fstat. If native lstat is unavailable, symbolic links and other
file types remain unsupported for LSTAT; it does not report a followed target as
a link. The cosmetic date in legacy long listing text is omitted because the SDK
lacks localtime_r; protocol attributes retain numeric timestamps.

SFTP uses the app's filesystem permissions. `/data` is the initial directory,
not a sandbox, jail or confinement boundary. This is file transfer, not SCP or
support for executing external programs.

Host evidence: eight SFTP cases pass on macOS and Linux, including actual OpenSSH
put/get, paged listings, metadata, independent cwd, file/directory handle caps,
disconnect cleanup, slow-reader window backpressure and oversized-packet rejection. The owned C++ service also
passes these cases with ASAN/UBSAN; the crypto C archive was not instrumented.
Native phat 13.60 acceptance passes 1 MiB Paramiko and actual OpenSSH transfers,
75-file directory paging, app0 listing, stat/chmod/rename, and cleanup of temporary
files and authorization. Native slow-reader backpressure also passes with a 32 KiB SSH receive window. Other firmware and symbolic-link metadata remain unverified.

## Reproduce host checks

Requires Python 3.12+, Clang, ar, OpenSSL and pinned archives. Fetch with
`python3 tools/prepare-ssh.py --archives-only` when the cache is absent. Builds
verify SHA-256, extract fresh platform-specific sources and perform no deployment.

```sh
uv venv --python 3.12 .local/dev/ssh-venv
uv pip install --python .local/dev/ssh-venv/bin/python paramiko==4.0.0
.local/dev/ssh-venv/bin/python tools/build-owned-ssh-host.py
.local/dev/ssh-venv/bin/python tests/ssh/test_owned_server.py
.local/dev/ssh-venv/bin/python tests/ssh/test_owned_service.py
.local/dev/ssh-venv/bin/python tests/ssh/test_owned_sftp.py
```

Linux can use a normal virtual environment; set HOST_CC/HOST_CXX for versioned
Clang. Eight transport and eleven service interoperability cases cover short IO,
backpressure, authentication, persistence and key management. GitHub Actions is
configured for these checks; no CI run is claimed by local results.

## Remaining release gates

- GUI client integration and additional firmware testing.
- Real PTYs for programs running natively on the PS5, which remain blocked by the
  native process limits in the capability record. The `linux` login provides a real PTY.

Retain dependency licenses and corresponding source as described in
[SSH-SOURCES.md](SSH-SOURCES.md).

## Public-key login (owned build)

The owned service supports Ed25519 and ECDSA P-256/P-384 keys. The optional
`/data/ps5term/ssh/authorized_keys` must be a private regular file (0600), at most
8 KiB and eight keys. Empty/missing means password-only. Blank lines, comments
and CRLF are accepted. Key options, certificates, RSA, unsupported curves,
malformed encodings and unsafe files fail startup rather than silently discarding
restrictions. Never copy a private key to the console.

From your computer, enroll your existing **public** key over an authenticated SSH
exec channel (enter the password shown in Settings):

```sh
command ssh -T -p 2222 ps5term@192.168.86.99 "ssh-key-add $(cat ~/.ssh/id_ed25519.pub)"
command ssh -T -p 2222 -i ~/.ssh/id_ed25519 ps5term@192.168.86.99
```

To remove that authorization, send the same public key with `ssh-key-remove`.
These management commands are exec-channel commands, not built-in interactive
shell commands. Updates validate the complete current file, refuse independent
key changes until SSH is restarted, write a private exclusive temporary file and
atomically replace the config before updating the live allowlist. New connections
see the change immediately. Existing authenticated sessions remain connected;
disable SSH in Settings to close them. Password authentication remains available.
The managed file contains canonical key records; comments are not retained when
keys are changed. A pre-existing `authorized_keys.update` is refused, not replaced.

Host evidence: eleven service interoperability cases pass on macOS and Linux, including actual
OpenSSH, signed-key validation, enrollment/revocation, persistence, key-count limits
and changed-file refusal. Native phat 13.60 tests also pass authenticated enrollment and actual OpenSSH
login for ECDSA P-256 and Ed25519, removal and rejection of the removed key.
Password and host identity remain unchanged. The temporary test authorizations
were removed. No result here establishes support on other firmware.
