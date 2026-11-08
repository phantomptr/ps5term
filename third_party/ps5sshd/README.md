# ps5sshd

SSH + SFTP server payload for the PS5, built on
- [wolfSSL](https://github.com/wolfSSL/wolfssl)
- [wolfSSH](https://github.com/wolfSSL/wolfssh)
- [shsrv](https://github.com/ps5-payload-dev/shsrv)

It offers faster transfer speeds than [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), but honestly, I just wanted to avoid implementing FTP support in [Gusco](https://intestinate.com/gusco/) ;)

Might as well move things forward...

## Running

Load ps5sshd.elf using your favorite method.

The server listens on port 22 (falls back to 2222 if 22 is taken).
On first start, it creates `/data/ps5sshd/` with host keys (ed25519 + ecdsa).
If neither `authorized_keys` nor `password` exists, a random 4-digit PIN is generated and stored in `/data/ps5sshd/password`.
As long as the password remains a 4-digit PIN, it is shown as a notification on every start.

```sh
sftp <ps5-ip>             # any user name is accepted
ssh  <ps5-ip>             # interactive shell
ssh  <ps5-ip> help        # list supported command set
```

To change the password from the shell:

```sh
echo "my new password" > /data/ps5sshd/password
```

The following shsrv commands have been improved:

```sh
chmod -R 777 /data/homebrew/PPSA99169        # Recursively change permissions
rm -rv /data/homebrew/PPSA99169              # Recursively delete directories
```

## Configuration

ps5sshd stores files in `/data/ps5sshd/`:

| File                 | Purpose                                                 |
| -------------------- | ------------------------------------------------------- |
| `authorized_keys`    | OpenSSH public keys, re-read on every connection        |
| `password`           | Password (first line); delete to disable password login |
| `ssh_host_*_key.der` | Host keys (DER)                                         |
| `ps5sshd.log`        | Log                                                     |

## Building

```sh
export LLVM_CONFIG=/path/to/llvm/bin/llvm-config
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
export WOLFSSL_DIR=/path/to/wolfssl-5.9.4-stable
export WOLFSSH_DIR=/path/to/wolfssh-1.6.0-stable
export SHSRV_DIR=/path/to/shsrv
make
```

## Notes

- The PS5 kernel has no pseudo-terminals. When a client requests a pty, ps5sshd handles echo/line editing itself (backspace, Ctrl-U, Ctrl-W, Ctrl-C kills the running command, Ctrl-D exits). Full-screen programs will not work.
- One session (shell, command, or SFTP) is allowed per connection; SSH connection multiplexing (`ControlMaster`) is not supported.
- Each connection runs in its own forked process.
- The PS5 filesystem is very slow with small writes (~14 MB/s at 32 KiB, ~190 MB/s at 4 MiB), so SFTP uploads are collected into 4-MiB blocks.
  The blocks are written by a separate writer process (shared memory + descriptor passing), allowing disk writes to overlap with the network.
  Write errors (e.g. disk full) are reported late, at the latest when the file is closed.
  Create `/data/ps5sshd/sync_writes` to write synchronously instead (~75 MB/s uploads).

## License

GNU General Public License v3.0 or later.
See COPYING for the full text.
