# Native disk usage implementation plan

User delegated implementation decisions and opted out of design questions.

Add `du [-shk] [path]` to the app-owned native shell. This first version always
prints one summary, in allocated KiB by default (`-h` formats bytes). It counts
hard-linked nondirectories once, includes hidden entries and directory blocks,
and never descends through symbolic links. One operand defaults to the session
working directory. Errors produce no misleading partial summary. Linux already
has a full du; this adds storage inspection without starting that guest.

Traversal lives in src/platform/disk_usage.hpp/.cpp, separate from shell parsing.
Use the existing safe native metadata adapter via a callback and DirectoryCursor.
Allocate cursor frames only for active depth, cap depth at32, entries at100000 and
hard-link identities at2048. At most32 directory entries are visited per tick.
Cancel/reset/destruction release every directory and allocation. Native lstat
restrictions must remain visible; do not follow symlinks as a fallback or invoke
raw syscalls. Host tests are separate from physical phat proof.

- [x] Add failing shell regressions for totals, hardlinks, symlinks, hidden files,
      pipelines, invalid input, cancellation and descriptor hygiene.
- [x] Implement traversal and shell dispatch; test depth/entry/hardlink limits.
- [x] Focused Mac/Linux and sanitizer checks, native build, documentation/package.
- [x] Guarded phat-only update, observed Home launch, real native read-only test,
      guest preservation; release owned resources and checkpoint evidence.

Do not touch Pro, publish, commit, change privilege or modify user files to test.

Verified: Mac/Linux terminal suites, ASAN/UBSAN shell and traversal tests, ten
SSH interoperability tests, native build/package and physical phat acceptance.
Evidence is in the private du-20261107-release acceptance directory.
