# Native system tools: ps, kill, uptime, free, nproc, sysinfo

Status: complete (2026-11-07). Verified on the phat (13.60): `ps` sees only the app,
with a verified record layout (pid, ppid 52, uid 1); kern.sdk_version is 0x13600007;
hw.physmem, hw.realmem and the vm.stats memory counts do not exist on the PS5 kernel,
so `free` reports the app's own memory. A read-only `sysctl` command was added.

## Goal

Give the native `ps5term` shell the everyday system commands that it is missing,
without starting Linux, using only calls the console already accepts from the app.

## Decisions

- All data comes from `sysctl`/`sysctlbyname`, `kill`, `getpid`, `getppid`,
  `getuid` and `clock_gettime` in libkernel. These are exported on the console
  (ps5upload's payload uses the same process sysctl on 13.60) and need no raw
  syscalls. `df` stays out: `fstatfs` returns EPERM in the app.
- `ps` reads `kern.proc.proc` (`CTL_KERN, KERN_PROC, KERN_PROC_PROC`) and parses
  FreeBSD amd64 `kinfo_proc` records with fixed offsets. The native build
  `static_assert`s those offsets against the SDK's `sys/user.h`.
- Other firmware may change the layout. Each snapshot checks the app's own record
  (pid, ppid, uid) against `getpid`/`getppid`/`getuid`. If they disagree, `ps`
  prints only PID and COMMAND and says the layout is unverified, instead of
  printing wrong numbers.
- If the app can see only itself, `ps` says so rather than implying an empty system.
- `kill` accepts `-SIG`, `-s SIG`, `-l` and signal 0. It refuses pid <= 1 and the
  app's own pid (closing the app is done from the Home screen). Other refusals come
  from the kernel and are reported as-is.
- `sysinfo` prints model, CPU count, memory, kernel/OS strings, SDK version from
  `kern.sdk_version` when present, uptime, app pid/uid, and what the app can do
  (process list visible, signals, child processes known unsupported). Missing
  values print `unknown`; nothing is inferred from host machines.
- Host builds (macOS/Linux tests) get a self-only process list and host sysctls,
  so shell tests stay deterministic.

## Steps

1. `src/platform/system_info.{hpp,cpp}` with a pure `kinfo_proc` parser, layout
   check, signal-name table and formatting helpers. Host tests with synthetic
   records, including truncated and malformed buffers.
2. Wire `ps [-f]`, `kill`, `uptime`, `free [-h]`, `nproc`, `sysinfo` into
   `BuiltinShell`, add help text, shell tests.
3. Mac and Linux terminal suites, native build, export audit, package.
4. Phat: guarded install, reopen, readiness, then SSH checks: `ps` lists the app
   with matching pid/uid, `kill -0` on a system pid reports allowed or EPERM,
   `kill` on pid 1 and self is refused, `sysinfo` shows 13.60-era SDK version.
