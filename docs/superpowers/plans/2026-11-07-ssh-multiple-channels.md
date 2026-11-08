# Bounded concurrent SSH session channels

Extend the verified sequential-channel release without changing native shell or
Linux guest semantics. A transport owns at most four native/Linux session states.
Every state owns its command, cwd, queues, PTY size, EOF and exit state, guest handle
and close deadline. Authentication/credentials and socket stay transport-owned.
Iterate channel states fairly; a receive-window-stalled channel must not block its
siblings. Never retain a wolfSSH channel pointer across worker calls.

SFTP stays exclusive and must be the first and only channel. Native/Linux channels
cannot be added to it, and SFTP cannot be requested on a multi-channel or reused
shell transport. TCP forwarding remains refused. The guest's existing shared PTY
slot limit still applies. A peer ignoring a close for ten seconds loses its entire
transport; this bounds library channel retention and is documented explicitly.

1. Write failing native tests for independent simultaneous shell directories,
   per-channel stderr/status, a slow reader alongside quick commands, and channel
   slot reclamation; add guest routing/resize/EOF tests.
2. Add a reproducible patch exposing per-channel exit-status send, idempotent under
   short writes. Apply to pinned source in host and native build scripts; no vendor
   source edits or private wolfSSH structures in the app adapter.
3. Move channel-owned state into four lazily allocated Session objects, dispatch
   requests by local channel ID, service each fairly and reclaim after peer close.
   Keep SFTP's existing read/shutdown path exclusive.
4. Run adapter/service/SFTP tests, sanitizer checks, Mac/Linux terminal suites,
   native SDK build and package integrity checks. Under the phat test-window lock,
   stop Linux cleanly, activate with backup/readback, launch from the observed Home
   tile and validate concurrent native/Linux/OpenSSH clients. Preserve Alpine files.
5. Update accurate feature limits, stop owned test resources and checkpoint results.

Only phat 192.168.86.99 firmware 13.60 is a physical acceptance target. No Pro,
firmware changes, generic launch, raw loader payloads or publication are in scope.

## Acceptance completed 2026-11-07

All five steps completed. Host SSH 35 tests and SFTP eight tests passed; the 35
SSH tests also passed with adapter ASan/UBSan instrumentation. Explicit rekey test
passed separately with and without sanitizers. Mac and Linux terminal suites,
format checks, native SDK build, ZIP CRC and current build-input hashes passed.

Phat firmware 13.60, build `fdbe05c7ad1c`, PID 256: four native channels, fifth
refused, stalled 273464-byte read alongside quick commands, independent cwd,
transport rekey and sibling close survival passed. Two concurrent Linux PTYs
retained independent sizes, accepted resize and an overlapping exec, and survived
sibling close. Existing Alpine packages and both proof files persisted. OpenSSH
ControlMaster kept a shell alive during three exec requests with TCP fallback
disabled. Hardware evidence is private under
`.local/dev/claude-20261104/hw/multiplex-20261107-release/`.

Primary Alpine and SSH remain on. Owned Remote Play stream 2 and Docker builder
were stopped, with fresh state checks. No Pro, USB, named GUI terminal integration,
other firmware or publication acceptance is implied.
