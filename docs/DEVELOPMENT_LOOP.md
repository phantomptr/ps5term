# PS5Term development loop

Only **phat `192.168.86.99`** is authorized. Pro is excluded. Explicit target validation runs before networking; no engine defaults, discovery or target fallback are used.

## Host checks and build

```sh
# Linux build container, mounted at /work:
docker exec ps5term-build bash -lc 'cd /work && make test HOST_CXX=clang++-18 HOST_CC=clang-18'
docker exec ps5term-build bash -lc 'cd /work && bash tools/build.sh Folder'
# Portable terminal/input/service checks on macOS:
TEST_OUTPUT_DIR=.local/dev/tests-macos bash tools/test-terminal.sh
python3 -m unittest discover -s tests -p 'test_hardware_cycle.py' -v
```

A generated public-input digest is embedded in the app and written to `assets/build-info.json`. It includes sorted compile definitions and is a source/assets/tooling/configuration identity, distinct from the final `eboot.bin` digest and the whole-folder artifact manifest. A workflow definition is not evidence of a GitHub CI pass.

## Candidate staging

```sh
python3 -m tools.dev.hardware_cycle --addr 192.168.86.99 --app-dir dist/FAKE99260
```

This command snapshots the candidate into ignored `.local/dev/evidence/<run>/candidate`, checks title and paths, then transfers it through ps5upload to a unique `/data/ps5term-dev/staging/FAKE99260/<run>` directory. It verifies every file hash and EOF. Any failed job, timeout or readback mismatch prevents further progress. It **does not replace, close or launch the installed app**. Runtime data under `/data/ps5term` is untouched. Failed staging directories are retained for diagnosis rather than broadly deleted.

The client requires a loopback engine URL, rejects redirects and uses an API allowlist. Kernel changes, loader replacement, reboot and generic app launch are unavailable through this client. Every request carries the phat address, including engine-local job queries. Do not substitute the old `.local/deploy-nolaunch.py` during automated testing: it does not implement these staging and ownership contracts.

## Coordination and launch gate

Every console test must hold `.local/dev/console.lock` using `TestWindow`. The lock is advisory: another agent must use the same file or explicitly coordinate its test window. It cannot detect unrelated programs that ignore the lock. Before activation, establish a working observed-state launch path and ensure no other agent owns the console.

The existing engine `/api/ps5/app/launch` can fall back to ShellUI injection and is refused. Remote Play UI is a candidate safe route only after its picture/input work on the explicitly selected phat. Never send blind controller sequences. Manual launch remains `awaiting-manual-launch`, not an automated pass.

On 2026-11-01 the running ps5upload kbmtest app selected Fat and displayed FW13.60/helper10.3.0. Remote Play start failed with `the console never sent a session id over ctrl`; no controller input or fallback launch was attempted. This is a launch-automation blocker, not evidence that PS5Term or its build failed. The read-only hardware endpoint returned `CFI-1115A 01X` and firmware word zero, so that endpoint alone cannot establish current firmware.

After the user released the other session, a retry reported a 31-second teardown cooldown. A second attempt after that cooldown again failed to receive a session ID. A development-only direct-launch probe was sent once through ps5upload's companion-payload route; it exited at `no-foreground-user` before calling the launch API. Process and focus checks showed PS5Term absent. No installed files were replaced. The probe is separate from the shipping app and provides no evidence that an app-owned backend works.

The staging harness also completed a physical-console transfer and hash/EOF readback, recorded under `.local/dev/evidence/`. This proves candidate staging only; it does not prove application launch or rendering.

Later that session, the user signed in on phat and Remote Play produced live
video. Selecting the installed PS5Term and sending the normal confirm input
launched it and displayed its terminal UI. Independent process/focus checks
confirmed `FAKE99260` in the foreground. Observed controller launch is therefore
available. A later verified candidate activation and two observed launches passed
build/PID/instance/foreground checks; see the native capability result record.
This does not validate the new standalone backend.

`tools/dev/activation.py` now verifies and backs up the installed app before stopping
its exact title/PID, installs a private candidate snapshot, verifies every file,
and removes only obsolete manifested regular files. A failed installation attempts
verified restoration from the backup. Recovery can still fail if the transport is
unavailable; inspect `activation.json`. Live activation and reopening passed on phat;
partial-transfer rollback has mocked coverage. A real refresh hit a duplicate held-metadata leftover; backup bytes were restored, but exact file-list verification required the narrowly verified duplicate cleanup described in the capability record.
Never kill all `sh` processes or guess helper ownership from a name substring.
Legacy helpers are left alone; their lifetime remains a separate acceptance gate.

To activate an already built candidate, after checking the observed UI launch route:

```python
from tools.dev.activation import activate_candidate
from tools.dev.hardware_cycle import TestWindow
from tools.dev.ps5upload_client import Ps5UploadClient
with TestWindow('.local/dev/console.lock'):
    result = activate_candidate('dist/FAKE99260', Ps5UploadClient('192.168.86.99'),
                                '.local/dev/activation')
```

This function never launches. Observe the home screen and confirm PS5Term through
Remote Play separately. Do not infer controller focus from an old screenshot.

## Readiness evidence

With `/data/ps5term/diagnostics.enabled` present, the app writes owner-only, bounded `/data/ps5term/status.json` at most once per five seconds. It contains schema/build/instance/PID and actual line-backend connection/requested-SSH state; it contains no password, token, key or command history. It is opt-in, and a filesystem failure leaves the UI running. Atomic rename provides complete-record visibility, not power-loss durability.

Readiness requires the expected build, a new instance, the matching live title/PID, and independent foreground evidence. HTTP success alone is never sufficient. A status record remains on disk after exit: reject stale records and independently inspect process presence. `tools/dev/app_diagnostics.py` assembles this record from the report, process list and focus endpoint; stale/wrong-build/unfocused records are rejected. The app/helper permission boundary requires making only the secret-free reports readable immediately before retrieval. `ssh_requested` is a preference, not authenticated server health.

## Reference capability checks

`tools/dev/posix_reference_probe.cpp` exercises real host identity, pipe/poll,
spawn/wait, an owned process group, PTY/termios, resize, and cleanup. Compile it
with `src/platform/probe.cpp` and `src/platform/capabilities.cpp`, using `-Isrc`
and C++20. On 2026-11-01 all six applicable operations passed on macOS, Linux,
and the local FreeBSD VM with successful cleanup. SceNet/POSIX descriptor
interoperability remains unknown. Every result is marked `ps5_evidence:false`.

The first FreeBSD run reproduced `EINVAL` when passing `O_NONBLOCK` to
`posix_openpt`; using the documented open flags and setting nonblocking mode
with `fcntl` passed on all three hosts. See the
[FreeBSD manual](https://man.freebsd.org/cgi/man.cgi?query=posix_openpt&sektion=2).
The reference probe is outside `src/` and is never linked into the app.

## Resume after interruption

Read `.local/dev/CHECKPOINT.md` first, then the foundation implementation plan. Logs and private evidence are under `.local/dev/`; no credentials or raw console captures belong in public docs. Usage resets do not schedule or restart the agent automatically. A new user message/session must resume from the checkpoint if execution stops.

## Native baseline probe

Build the opt-in variant with `APP_DEFINITIONS=PS5TERM_CAPABILITY_RESEARCH bash tools/build.sh Folder`
in the supported Linux environment. Through the guarded development client, write
exactly `baseline-v1\n` to `/data/ps5term/probe.request`. A normal release does not
run this service. The request is consumed once, and one bounded operation advances
per app tick. Identity and pipe/poll are measured; other operations remain unknown.
The report is `/data/ps5term/capabilities.json`.

The [native capability decision record](research/2026-11-01-native-capability-results.md)
separates real phat results from reference VM results and tracks the remaining
standalone-launch and PTY gates.
