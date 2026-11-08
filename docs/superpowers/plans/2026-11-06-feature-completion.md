# Feature completion sequence

User delegated design and requested implementation without questions. Preserve
other agents' work; phat only; use guarded ps5upload for device mutations.

1. Prove expanded ext2 volumes under the actual embedded Linux engine, not just
   host filesystem tools. Add a reproducible guest test using LinuxDataDisk.
2. Add optional persistent Alpine userland (pinned official RISC-V minirootfs,
   verified download, private staging, guest chroot, apk), keeping Buildroot as
   boot/recovery environment. Test apk/package persistence before promotion.
3. Expose storage/environment actions through keyboard, mouse and controller
   Settings; preserve original data and keep blocking disk work off the UI thread.
4. Improve controller clipboard actions, terminal search/zoom where the existing
   renderer permits; test input ownership and terminal resize behavior.
5. Build/package once changes pass checks; guarded phat stop/install/readback,
   observed normal launch and readiness. Preserve rollback and data backup.
6. Update feature matrix with evidence and remaining external hardware/client
   checks. Never equate untested firmware/physical USB handling with host tests.

Alpine is a userland inside the existing Linux VM, not a native PS5 jail, Docker
runtime, separate kernel, or security boundary. No automatic destructive migration.

## Result (November 7)

Delivered the storage, persistent Alpine/apk, text size and controller clipboard
increment. macOS terminal tests, Linux make test (75 Python cases included), native
SDK packaging and real-engine sanitizer guest tests passed. Phat 13.60 completed
Settings expansion, Alpine HTTPS installation, apk Bash installation, package and
file persistence through Stop/Start and final app update, plus controller copy/paste
and text size changes. Pro untouched; no remote CI or other-firmware claim.

Still outside this increment: terminal search, tabs/panes, additional native
utilities and firmware capability reporting, physical USB acceptance, named GUI
terminal integrations, and public source/store publication.
