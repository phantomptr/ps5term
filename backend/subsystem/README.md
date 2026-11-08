# In-process Linux runtime

This adapter is part of the default build. It embeds the pinned TinyEMU interpreter
without its command-line frontend. It does not start a process, allocate a host PTY,
install signal handlers or change host terminal state. It opens sockets and files only
through callbacks the caller supplies: the `net/` NAT, the data disk and the PS5 files
mount. The caller supplies known guest images in memory. Linux processes and PTYs exist inside the guest.

The API in `embedded_linux.h` is confined to one owning thread. The optional
`linux_session.h` layer owns that thread and provides synchronized bounded I/O,
resize, status and stop/join. Rendering/input threads must use that layer rather
than calling the core concurrently. Engine calls from different
instances are serialized by an atomic entry guard, without a native TLS import.
That guard does not make an individual instance's other calls thread-safe.

The prototype uses 128 MiB guest RAM. Its caller-selected engine heap limit has a
192 MiB maximum; tests use 160 MiB. Every pinned engine allocation, including
metadata, is tracked and counted. The owner plus 16 KiB input and 64 KiB output
rings adds approximately 82 KiB outside that limit. Caller image buffers and test
transcripts are also outside it. The worker adds another 80 KiB of byte rings,
small control/transfer buffers and a platform-managed pthread stack, also outside
the engine limit. The root disk is copied into the tracked domain,
accepts at most 32 MiB, and never changes the supplied image. Guest disk writes are
ephemeral, not persisted automatically.

The compiler force-includes `tinyemu_hooks.h` for engine objects only. Allocator,
fatal exit, abort and assertion paths stay within a C recovery boundary. Engine
debug printing is suppressed; guest output goes to a bounded ring. The arena
teardown frees the complete graph even after partial initialization. It avoids
upstream's incomplete machine destructor and assumes **all** resources in this
restricted profile are memory owned by that arena, apart from the caller-owned
callbacks above, which the caller releases after the worker joins. Display, guest input
devices and async disk backends are not instantiated. Any engine update or new device requires re-auditing that assumption.

Each step accepts at most 50,000 interpreter cycles. Timers are polled before
stepping so WFI can wake. Requested terminal sizes are deferred until the guest
virtio console has an input queue, avoiding a reproduced early-interrupt boot
stall. Input reports partial acceptance when full. When an output descriptor will
not fit, the small virtio patch leaves it pending. The next step retries it and
pauses guest execution until output space returns. A descriptor larger than the
entire 64 KiB ring is still an explicit error. No partial descriptor is acknowledged
or silently dropped. The worker sleeps while both output rings are full and wakes
on reads or stop. Idle WFI sleep hints use timed condition waits of at most 10 ms.

`start` copies image metadata and borrows the immutable image bytes until `stop`
has joined. It reports thread creation, not successful Linux boot. Natural guest
exit enters DRAINING and preserves final console bytes before reporting STOPPED
or FAILED. Readers must keep draining; explicit stop cancels that wait, discards
queued I/O and joins the worker. Concurrent stop callers join exactly once. Do
not race destruction with any API call or race a new start with stop. This is an
owned worker, not pthread cancellation or a hard wall-clock shutdown guarantee.

This interpreter is for trusted images. Memory limits and recovery hooks do not
establish isolation against a malicious guest or interpreter memory corruption.
Cycle batches also do not prove a wall-clock bound on malformed device requests.
No jail/container security claim is made.

Build and test from the repository root (download pins first with
`python3 -m tools.dev.embedded_linux --download` if absent):

```sh
python3 -m tools.dev.build_embedded_runtime --sanitize --output build/runtime-host-check
# Separate race-detector build (requires a supported host TSan runtime):
python3 -m tools.dev.build_embedded_runtime --thread-sanitize --output build/runtime-thread-check
# macOS Apple silicon with Rosetta already installed:
python3 -m tools.dev.build_embedded_runtime --sanitize --host-arch x86_64 --output build/runtime-x86-check
# Linux Clang18 and the repo's existing cached SDK; compile/link only:
python3 -m tools.dev.build_embedded_runtime --native-objects --output build/runtime-ps5-check
```

Each command requires a fresh output directory and uses at most two compiler
workers. Host acceptance has a 120-second process deadline. It boots five times,
runs real shell functions/pipelines/fork-wait and PTYs, resizes the live console,
tests 40 construction allocation cutoffs plus runtime OOM, fills both queues and
checks that input images remain unchanged. Worker acceptance additionally tests
repeated start/stop, concurrent stop/join, startup cancellation, live resize,
12000-line output through a non-reading client, stop while blocked, final output
on natural guest shutdown, and startup OOM. Linux enables ASan leak detection;
Apple's supplied sanitizer runtime does not support that feature. The adapter is
fully ASan/UBSan instrumented. Engine signed arithmetic uses `-fwrapv`; upstream
signed-left-shift sign extension was reproduced under UBSan, so **shift-base**
checks are disabled for engine objects only. Other engine checks remain enabled.
An upstream zero-length `memcpy` with a null FDT-property pointer was separately
reproduced on Linux UBSan and fixed in the small embedding patch.

The PS5 result is an x86-64 relocatable core plus a shared **link-check** object
against public SDK stubs. Neither is an app/payload or runtime proof. It confirms
that the core needs ordinary allocation/string operations, pthread operations,
`clock_gettime` and the compiler helper `__udivti3`; no native fork, PTY, mmap, host console or setjmp
imports remain. Runtime timing/ABI/memory behavior still needs phat-only app
validation through the normal PS5Upload/Home launch workflow.

Before product integration: connect the adapter to the native renderer/input; prepare
a modern reproducible guest with complete license/source delivery; and run an
opt-in native lifecycle probe. Keep native and Linux environments explicit. Guest
root does not grant PS5 root or native filesystem/device access. The old upstream
2018 sample must not be shipped as a release environment.

The experimental `linux_terminal.hpp` adapter now connects the worker to the
existing VT model. A UI-thread pump moves at most 16 KiB in each direction and
retains unaccepted suffixes; keys, paste, mouse reports and terminal-query replies
all use the model's byte queue. Resize updates both model and guest. Start creates
a fresh model; stop/destruction synchronously joins, so frontend teardown must
account for that outside its ordinary frame pump. The native renderer uses this
layer; SSH sessions use the separate agent console through `src/platform/guest_agent.cpp`.

Run the real guest/VT acceptance against a verified host runtime build:

```sh
python3 -m tools.dev.build_linux_terminal --runtime build/runtime-host-check --output build/terminal-host-check
```

The test exercises shell output through cells, real guest terminal size, a device
status reply round trip, Ctrl-C interrupting sleep, and clean restart. macOS and
Linux ASan/UBSan acceptance passes; Linux additionally checks leaks. Native adapter
runtime and application input routing remain separate gates.
