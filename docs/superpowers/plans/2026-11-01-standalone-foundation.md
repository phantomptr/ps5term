# PS5Term Standalone Foundation Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task in the current session. Do not spawn agents or commit/push unless the user explicitly requests it. Steps use checkbox syntax for tracking.

**Goal:** Establish a reproducible phat-only test loop and prove the app-owned helper/session foundation before replacing the working shell and SSH adapters.

**Architecture:** Keep existing input/rendering stable. Add a guarded development harness, explicit platform capability records and an owned launch/session boundary; retain the current external-loader backend only as a labeled development fallback during bring-up.

**Tech Stack:** C++20 application, C platform helpers, Python 3 development tools, existing native ELF/FSELF tooling and Linux host tests. libvterm/wolfSSH integration follows the platform gate.

**Spec:** `docs/superpowers/specs/2026-11-01-ps5term-design.md`

## Global Constraints

- Hardware development is restricted to phat `192.168.86.99`, firmware 13.60. Do not contact or modify Pro `192.168.86.100`.
- Every hardware request includes the explicit address; no default selection/discovery/fallback.
- Preserve input-agent work and the uncommitted repository. No reset/clean, commits or pushes.
- Do not require external 9021 in the standalone release; development fallback never counts as acceptance.
- Unknown firmware/platform behavior is a gated investigation, not an implied supported feature.
- ps5upload is the deployment/test driver, not an end-user runtime dependency.
- No automatic system-UI injection, console reboot, jailbreak changes or kernel mutation as recovery.

## Review Focus

- Engine default targets Pro: harness rejects absent or disallowed target before making any request (Task 2).
- Transfer is incomplete or retains obsolete binaries: hash/file-list mismatch prevents launch, cleanup touches only prior manifested files (Task 2).
- Launcher accepts a request but title is missing/unfocused: readiness requires instance/build identity and focus, not HTTP success (Task 3).
- Helper survives app failure: owned-generation lease and child cleanup are exercised before standalone acceptance (Task 5).
- An API exists in headers but fails from the app: probes preserve errno/context and report unsupported instead of inferred success (Task 4).

## Task 1: Make the baseline test command representative

**Files:** Modify `Makefile`, `tools/test-terminal.sh`, `tests/terminal/test_settings.cpp`, `docs/TESTING.md`; create `.github/workflows/host-tests.yml`.

**Interfaces:** `make test` runs renderer, Python tooling and terminal suites. `HOST_CXX`/`HOST_CC` select host compilers; no suite contacts consoles. Use platform-correct dead-strip/linker options for macOS versus Linux.

- [x] Reproduce the documented host settings declaration failure in an isolated test output directory; do not modify app code to accommodate the stand-in.
- [x] Correct the stand-in declaration to match the platform prototype, preserve assertions, and make terminal test outputs selectable with `TEST_OUTPUT_DIR` (default `build/tests`).
- [x] Wire terminal tests into `make test`; make the Clang 18 compiler selection explicit in the Linux CI job.
- [x] Verify all existing suites in the documented Linux environment and focused portable suites on Mac; record commands/results separately. Workflow creation is not a CI pass until it actually runs. Update inherited CI claims accordingly.

## Task 2: Phat-only candidate deployment with evidence

**Files:** Create `tools/dev/console_target.py`, `tools/dev/ps5upload_client.py`, `tools/dev/hardware_cycle.py`, `tests/test_hardware_cycle.py`, `docs/DEVELOPMENT_LOOP.md`; read existing `.local/deploy-nolaunch.py` as reference only.

**Interfaces:** Python `require_phat(addr: str) -> str` accepts only `192.168.86.99`; `Ps5UploadClient.request(method: str, path: str, body: dict | None) -> dict` injects/requires the approved address and checks job/error responses. The client constructor requires `addr`; it never reads the engine default. `deploy_candidate(app_dir: Path, client: Ps5UploadClient, evidence_dir: Path) -> dict` returns manifest/readback results and does not launch.

- [x] Add mocked tests `test_rejects_pro_before_network`, `test_rejects_missing_target`, `test_job_failure_prevents_launch`, `test_readback_hash_mismatch_prevents_launch`, `test_obsolete_cleanup_is_manifest_scoped`, `test_timeout_preserves_known_good`.
- [x] Implement an exclusive test-window lock shared with coordinated app work; fail on lock contention. Keep artifacts, lock and evidence under ignored `.local/dev/`, not public docs. Capture candidate hashes, build identity and redacted source snapshot identifier without private `.local`/dump content.
- [ ] Preflight selected engine/helper, exact phat model/FW evidence and title identity; close the title, confirm exit, and stop only owned helpers identified by name plus executable/generation metadata. Do not broadly kill by name substring.
- [x] Implement transfer-job polling, exact file readback hashes and previous-manifest-scoped stale file cleanup. Preserve writable `/data/ps5term` and known-good app backup. Timeouts/failures leave a deploy failure record and never continue to launch.
- [x] Run mocked tests with `python3 -m unittest discover -s tests -p 'test_hardware_cycle.py' -v`. No hardware actions occur from host tests.

## Task 3: Verify close/reopen using a safe launch adapter

**Files:** Extend `tools/dev/hardware_cycle.py`, `tools/dev/ps5upload_client.py`, `tests/test_hardware_cycle.py`, `docs/DEVELOPMENT_LOOP.md`; add `src/build_info.hpp`, `src/diagnostics.hpp`, `src/diagnostics.cpp`; modify `tools/build.sh`, `src/app.cpp` for bounded diagnostic output/build identity.

**Interfaces:** Harness `launch_candidate(strategy: str, client: Ps5UploadClient, expected_build: str) -> dict`; strategies initially `manual` and `remoteplay-ui`. There is no generic engine-launch strategy. Diagnostics `write_status(const DiagnosticSnapshot &snapshot) noexcept` writes a bounded owner-only status record with schema/build/instance/capability state; no secrets. `DiagnosticSnapshot` fields are fixed in this task's header and versioned.

- [x] Add tests `test_launch_http_success_is_not_readiness`, `test_wrong_build_rejected`, `test_old_instance_rejected`, `test_no_focus_not_passed`, `test_launch_timeout_does_not_retry_or_touch_shellui`, `test_manual_launch_is_reported_manual`.
- [x] Record ps5upload input/video availability and the native title's actual launcher path. Verify process presence, visible foreground and build/instance identity independently.
- [x] Implement observed-state Remote Play navigation only after its input and screenshot capabilities are available. Never send blind button sequences against unknown focus. The existing engine `/api/ps5/app/launch` route remains excluded while it has the known ShellUI fallback.
- [ ] If Remote Play cannot provide reliable reopen, report automated relaunch blocked and retain manual home-screen launch. A future no-fallback ps5upload API is a separate coordinated change, not part of this repository's assumed implementation.
- [x] In an exclusive phat test window, run one close/deploy/readback/reopen cycle, then a second reopen to verify stable identity/cleanup. User authorization in this session covers normal phat deployment and app testing; recheck active-agent coordination, not repeated generic permission. Record actual launch method and distinguish automation from manual evidence.

## Task 4: Native app-context capability probe

**Research inputs:** The SDK stdio sample and elfldr bootstrap are references from payload contexts, not proof of native-app permissions or PTY support. No additional kernel/HV/A53 dump is a prerequisite.

**Files:** Create `src/platform/capabilities.hpp`, `src/platform/capabilities.cpp`, `src/platform/probe.hpp`, `src/platform/probe.cpp`, `tests/terminal/test_capabilities.cpp`; integrate a development-only action through `src/research.*` without changing input mappings; extend test script.

**Interfaces:** `CapabilityState { unknown, supported, unsupported, failed }`; `CapabilityResult` includes operation, state, native error and process context; `CapabilityReport` includes schema, firmware/model/library IDs and results. `ProbeRunner::tick(std::uint64_t now) noexcept` advances one bounded step; `report() const noexcept -> const CapabilityReport&`. Production startup does not run the research probe automatically.

- [x] Add tests for unsupported operations, partial report recovery, cancellation cleanup and context differences using fake platform calls. Missing callbacks yield unknown, never supported.
- [x] Inspect exact APIs and SDK helpers offline before selecting each live probe. Separate metadata-only reads from operations that allocate processes/PTYs or touch devices. Do not sweep arbitrary ioctls or create jails in this first probe.
- [ ] Implement short-lived identity, pipe/poll, spawn/wait, signal/group, PTY/termios and resize checks with cleanup and limits. Preserve app and child contexts separately. Detect SceNet versus kernel descriptor assumptions explicitly.
- [ ] Run in Linux/FreeBSD references to validate report/lifecycle behavior, then on phat through the verified deployment loop. Record actual unsupported/error results; no PS5 inference from VM success.
- [x] Produce the adapter decision record with concrete evidence for Gate A and Gate B in `docs/research/2026-11-01-native-capability-results.md`. If unavailable, record blocked capability and the next bounded investigation rather than inventing support.

## Task 5: Owned helper launcher, IPC and lifecycle

**Files:** Create `src/platform/launcher.hpp`, `src/platform/launcher.cpp`, `src/session.hpp`, `src/session.cpp`, `backend/broker/` launcher/IPC sources, `tests/terminal/test_session.cpp`, `tests/terminal/test_broker_protocol.cpp`; modify `src/connection.*` only after the platform decision.

**Interfaces:** `LaunchResult` carries typed failure or owned child/generation; `PlatformLauncher::start(const LaunchSpec&) noexcept -> LaunchResult`, `poll(ChildId) noexcept -> ChildState`, `stop(ChildId) noexcept -> StopResult`. `SessionId` carries slot/generation. `SessionManager` exposes `open`, bounded `write`, `resize`, `signal`, `close` and `poll` with explicit capability/status results. Exact PS5 mechanism is selected by Task 4 evidence; generic code never embeds firmware offsets.

- [ ] Add tests for partial IPC frames, >64 KiB length rejection, wrong token/version/generation, descriptor loss, backpressure, reused child identity, partial writes and exit status.
- [ ] Implement the selected launcher with typed error reporting and no external 9021 connection. Prefer inherited IPC; otherwise implement the spec's authenticated loopback handshake and temporary-secret lifecycle. Keep bootstrap failures actionable and reject unknown privileged layouts.
- [ ] Add the 10-second owner lease, idempotent stop, explicit cleanup/reap and bounded output. Test app EOF/crash, repeated start/stop, stale ownership and resume behavior with a fake clock.
- [ ] On phat, demonstrate helper start/data/exit, foreground app close and forced app death cleanup. Verify no external loader dependency and no unrelated process mutation. Do not remove the old working path until this passes.
- [x] Document Gate A/B outcome. If real native PTYs fail, expose the existing line backend honestly; full interactive-terminal claims remain gated. Do not substitute a pipe editor for POSIX job control.

## Subsequent subsystem plans

These milestones are part of the finalized spec but need their own bounded implementation plans after the platform results, rather than speculative kernel-specific tasks now.

| Milestone | Files/boundary | Required deliverable and tests |
| --- | --- | --- |
| M2 terminal | `src/terminal_model.*`, `src/session.*`, new cell renderer, existing input router and pointer adapter | Pinned libvterm, UTF-8/font pipeline, alternate-screen/transcript/resize/selection tests, command versus raw input, tabs/profiles and bounded queues. Preserve existing mouse/controller tests. |
| M3 SSH/SFTP | `backend/ssh/`, `src/ssh_service.*`, `src/settings.*`, new versioned config, `tools/prepare-ssh.py` replacement | PS5Term-owned wolfSSH service using shared session backend; persistent keys/password configuration, import without changing old ps5sshd, authenticated health, auth/exec/SFTP/port-conflict/disable/crash/resume tests. |
| M4 packages | `packages/recipes/`, package manifest/installer, build dependency lock | Existing tools inventory, first complete-shell candidate, capability/ABI gating, staged updates and rollback, dependency/license records, traversal/conflict tests; consistent local/SSH shell. |
| M5 environments | `backend/environments/`, environment image manifests and Settings adapters | Separate minimal jail and QEMU experiments; only expose successful backends. Serial boot, disk persistence, network and resource tests; host/guest identities and sharing explicit. |

Before each subsystem implementation, derive its exact interfaces/tests from the spec and the preceding capability results. This is deliberate sequencing, not a promise that the current PS5 kernel supplies the missing APIs.

## Design coverage and exit review

- Product/controller/keyboard/mouse requirements: M2 and existing input tests.
- Standalone launch and no external payload flow: Tasks 4–5, Gate A.
- Native terminal correctness and honest fallback: Task 4, Gate B, M2/M4.
- SSH ownership/config/auth/SFTP/lifecycle: Task 5 foundation and M3.
- Firmware/model support boundaries: Tasks 4–5 and every package/service manifest.
- Offline captures and ABI research: Task 4 inputs; no fresh broad dump requirement.
- ps5upload deploy/close/reopen/rollback: Tasks 2–3; launch automation is an explicit gate.
- CI/test command completeness: Task 1; host pass and real hardware results remain distinct.
- Packages/environments/store release: M4/M5 and spec release gates; no premature publication.

Review the design and this plan before execution. Any change to the standalone, firmware, phat-only or service-lifetime contract must update the spec and its acceptance checks together. Execution evidence and remaining platform gates are tracked in `.local/dev/CHECKPOINT.md` and the native capability decision record.


## Execution status — 2026-11-01

Tasks 1 and the app-file portions of 2/3 are verified. The lock is advisory, and
Remote Play is driven through observed computer-use actions rather than a blind
Python launch adapter. Legacy helper ownership/cleanup remains unverified, so the
Task 2 helper step stays open. Remote Play manual fallback was unnecessary once
the user signed in; no generic launch fallback was used.

Task 4 measured identity and pipe/poll in actual native app instances. Three
separately opted-in child modes through the audited normal `rfork_thread` binding
returned EPERM without creating children. This does not establish that every
native launch API is unavailable. PTY, process groups, resize and descriptor
interoperability remain unknown. Reference checks pass on three OSes. Task 5's
portable protocol/session/lease and native child-research adapters have host
coverage; standalone launch and physical helper-lifetime acceptance remain open.
No raw syscall wrapper or credential change was used. See the native capability
decision record for exact modes and evidence.
