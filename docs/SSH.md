# SSH in PS5Term

PS5Term is an installable native terminal app. SSH is an optional service under
Settings, off by default. It uses a bundled adaptation of ps5sshd; users do not
need to send its internal ELF separately.

## Install and use

1. Extract `PS5Term-00.001.000.zip` and copy the complete `FAKE99260` folder to
   `/data/homebrew/FAKE99260` using your existing homebrew installation workflow.
   Keep `eboot.bin`, `sce_sys`, `sce_module` and `assets` together.
2. Launch PS5Term through your homebrew launcher. The console must already be
   jailbroken with its compatible native app loader. The local basic interpreter
   does not need elfldr; this legacy SSH implementation still requires an ELF
   loader on port 9021 to start its bundled server.
3. Use the terminal normally. Press **Options** on DualSense or **F6** on a USB
   keyboard to open Settings.
4. Press **Cross / Enter** to turn SSH on. Wait for the screen to report that the
   bundled SSH server is listening; it will show SSH and SFTP connection commands.
5. Connect from your computer. Press **Triangle / F2** in Settings to show the saved password.
   Press it again to hide; leaving Settings hides it automatically. On port 22, the command is `ssh root@<ps5-ip>`; if the server uses
   fallback port 2222, it is `ssh -p 2222 root@<ps5-ip>`.
6. Press **Circle / Esc** to return to the terminal. SSH continues to run as a
   separate service. To switch it off, return to Settings and press Cross / Enter.
   The listener closes and server-owned SSH sessions disconnect within a few
   seconds. Independently launched research processes may continue running.

If starting fails, check that elfldr is running and press **Square / F5** in
Settings to retry. The app checks the bundled server's SSH identification string;
connecting to an unrelated SSH listener is not reported as success. Separately
installed ps5sshd processes are not deliberately terminated.

The preference is stored in `/data/ps5term/ssh.enabled`. It persists across app
launches. A missing flag means disabled. The app must have access to create and
remove this file. The bundled server watches it to honor switching SSH off.
Server host keys and authentication configuration live under `/data/ps5sshd`.

## Verify access

After login, run:

```sh
id
pwd
help
ls /data
```

The SSH username does not select a PS5 account. The server attempts to establish
root credentials and elevated PS5 capabilities. Confirm UID/EUID with `id`;
UID 0 alone does not establish access to every kernel or hardware subsystem.

## Warp client startup errors

Errors containing `command: command not found`, `then: command not found`,
`$WARP_TMP_DIR/.zshenv` or `esac: command not found` indicate that Warp's shell
bootstrap reached the limited legacy command interpreter. Authentication has
already succeeded at that point; the interpreter cannot run the Bash/Zsh startup
script. Its later `exit` can also close the connection.

From the **local Mac shell**, bypass Warp's SSH wrapper:

```sh
command ssh root@192.168.86.99
```

Use the port shown in Settings (`-p 2222` if applicable). Warp documents
`command ssh` as its [wrapper opt-out](https://docs.warp.dev/terminal/warpify/ssh-legacy#implementation).
Avoid enabling Warpification for this connection. macOS Terminal.app with plain
SSH is another option. After connecting, start with `help`, `pwd` or `id`.
The bypass has not yet been physically retested for this report.

This is a shell-language compatibility limitation, not a wrong-password error.
The owned SSH prototype also uses a bounded interpreter and does not yet supply
a complete POSIX shell; replacing the transport alone will not enable Warp's
Bash/Zsh integration.

## Public keys and new commands

Install your OpenSSH **public** key in `/data/ps5sshd/authorized_keys` using SFTP,
preserving any existing keys. Verify login in a second connection before removing
`/data/ps5sshd/password` to disable password authentication. Never upload your
private key. Retain host keys between launches so the server identity stays stable.

Compile new PS5 payload commands with the SDK and install their executable ELFs
under `/data/ps5sshd/bin`. The shell also searches `/mnt/usb0/homebrew/bin` and
`/user/homebrew/bin`. This is how researchers extend the shell independently of
the app UI. Linux binaries are not interchangeable with these payloads.
Full-screen terminal applications are not supported by this shell.

The local in-app console now uses its own basic interpreter (`help`, `pwd`, `cd`,
`echo`, `id`, `ls`, `cat`), so its commands and identity differ from this legacy
SSH shell. It cannot execute installed payload commands. An owned in-app SSH
service sharing the new session backend is still planned; bundling the legacy
server does not establish standalone SSH operation. The inherited `/data/ps5sshd`
configuration behavior above is a remaining migration issue, not the intended
PS5Term-owned service design.

## Build the app

On Linux with LLVM 18, make, Python 3.12+, patch, unzip, wget and xxd available:

```sh
make release
```

This builds both internal server assets, the signed native app folder, and the
installable ZIP. The native-app toolchain and runtime follow the recommended
boilerplate. Its bundled servers use pinned SDK v0.45. If building the internal
SSH asset alone, `PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make ssh` uses an existing
SDK installation. `make ssh` is a developer target, not the app deliverable.

## Verification status

Host socket tests exercise fragmented identification, refusal to identify a
foreign server as ours, missing-asset failure, and streaming a payload larger than
one buffer. Preference tests cover disabled defaults, persistence and write failure.
Native compilation and package checks are separate from physical-console proof.

On the fat PS5 running firmware 13.60, SSH authentication, command execution
(`id`: UID 1, effective UID 0), and a 256 KiB SFTP upload/download round-trip
have passed. The app was updated and relaunched while keeping SSH running.

The user also confirmed the password displays in Settings on the console.
Remaining physical checks include saved-preference display in Settings, USB
keyboard input, independently installed commands, and disabling
SSH with an active session. The disable test is deferred because the user asked
to keep SSH on.

Behavior across rest mode and after closing the app is not yet hardware verified.
No store release should claim verified 13.60 support before these checks pass.
