# SSH payload source provenance

- ps5sshd 1.1.0: <https://gitlab.com/intestinate/ps5sshd>, commit
  `227e09de41e574806f842c4b66aed66d78ff4a9d`, vendored unchanged in
  `third_party/ps5sshd`. Download archive SHA256:
  `c2acdc91967b6a87a87d9506438985d31e75fe5b7159c05d87036bff2bf968a8`.
- shsrv: <https://github.com/ps5-payload-dev/shsrv>, commit
  `6f320637d56d344a0e7797753099e33238bbf146`, existing vendored root sources and
  core command bundle in `third_party/shsrv`.
- wolfSSL v5.9.4-stable: <https://github.com/wolfSSL/wolfssl/tree/v5.9.4-stable>.
- wolfSSH v1.6.0-stable: <https://github.com/wolfSSL/wolfssh/tree/v1.6.0-stable>.
- PS5 payload SDK v0.45: <https://github.com/ps5-payload-dev/sdk/releases/tag/v0.45>.

Crypto archive URLs and SHA256 values are pinned in `tools/prepare-ssh.py`.
All four source projects above use GPL licensing; retain their notices and
provide corresponding source when distributing binaries. This repository's
vendored files, preparation script, pinned dependency sources and build script
describe the complete adaptation. Dependencies remain in `.deps/ssh` after a
build. Do not publish a binary-only store release without corresponding source.

Build-time adaptations: link only shsrv's core bundle; replace compilation date
and time macros with fixed strings; give the bundled process a distinct name and
SSH identification; watch the app-owned `/data/ps5term/ssh.enabled` preference
and terminate server-owned SSH sessions when disabled. Authentication and
privilege setup remain upstream logic. The resulting server is bundled inside
the native PS5Term app; it is not the user-facing deliverable.

The separate owned service (`backend/owned_ssh`, `tests/ssh`,
`tools/build-owned-ssh-host.py`) uses the same pinned wolfSSH/wolfSSL archives
directly, with PS5Term-owned authentication, channel policy and command adapters.
It imports no ps5sshd/shsrv runtime implementation. An opt-in native adaptation
is bundled and has passed phat 13.60 SSH and SFTP acceptance;
see [OWNED_SSH.md](OWNED_SSH.md) for its limits and reproducible host checks.

The owned SFTP profile compiles upstream `wolfsftp.c`. PS5Term patches directory
replies to bounded pages with failure cleanup; native port adapters provide the
SDK directory-record decoder and no-follow metadata fallback. These changes are
reproducible in `tooling/ssh/owned-sftp-bounds.patch` and
`tooling/ssh/native-sftp-directory.patch`, with adapters in `backend/owned_ssh`.
No source from ps5sshd is used in this runtime. The native long listing omits
cosmetic local dates; wire attributes retain timestamps. See OWNED_SSH.md for
limits, including unsupported native link metadata when lstat is unavailable.
