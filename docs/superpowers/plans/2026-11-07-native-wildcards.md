# Native filename wildcards implementation plan

User delegated design decisions and opted out of questions. Execute inline; preserve
other agents' work and all known-good releases. No commits or public publication.

Goal: native commands accept final-component `*` and `?` patterns without Linux.
Keep quote/escape information in an optional parser mask. A separate incremental
FilenameExpansion scans directories, sorts each pattern's matches by byte value,
keeps unmatched patterns literal, excludes dotfiles unless the component starts
with a dot, and treats resulting filenames as arguments, never shell syntax.

Use existing DirectoryCursor; never open matching files or use raw syscalls.
Support literal directory prefixes, including spaces and symlinks. Reject active
wildcards in directory components, bracket expressions and redirection targets
before opening redirections or executing any pipeline stage. Limits: 31 arguments
per stage, 32 KiB result storage, 100000 visited directory entries per pipeline,
32 work units per tick. Interrupt/reset/end-of-pipeline release all resources.
Linux remains the route for complete shell globbing and recursive patterns.

Files: backend/ps5term_pipeline.h (optional protection mask),
src/platform/filename_expansion.{hpp,cpp} (bounded scan/matcher),
src/builtin_shell.{hpp,cpp} (expansion before pipeline redirections),
tests/terminal/{test_builtin_shell.cpp,test_filename_expansion.cpp}, host link
lists, docs/FEATURES.md.

- [x] Add shell regressions; observe missing expansion failure.
- [x] Implement protected parsing and incremental expansion; verify quoting,
      mixed quotes, variables, spaces/operators in names, hidden files, no match,
      pipelines, cancellation, argument/entry limits and no redirect side effects.
- [x] Mac/Linux suites, focused sanitizer checks and native build/package.
- [x] Guarded phat-only deployment, real read-only wildcard/quoting and Alpine
      preservation acceptance. Clean owned resources and obsolete test outputs.

No Pro, privilege changes, firmware probes, or mutations of console user files.

Verified on phat firmware 13.60: controller wildcard entry, six native SSH
contracts and persisted Alpine apk/Bash/files. Mac/Linux suites, focused
ASAN/UBSAN, eleven SSH tests, seven distribution tests and native build passed.
Private acceptance: glob-20261107-release. Retired unused privilege helper and
legacy binaries; development scope and historical document banners are accurate.
