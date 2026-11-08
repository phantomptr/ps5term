# Embedded Linux feasibility spike

> Execute inline; preserve the current native app and input work. No console mutation for this spike.

Goal: determine whether a small software-interpreted Linux guest supplies real shell/process/PTY semantics without requiring the host PS5 app to fork. The app remains a native terminal; this optional guest is a distinct environment and never a claim of native PS5 process support.

1. Audit remaining native PTY evidence offline. Record ambiguity instead of turning a header/syscall number into a compatibility claim. Do not repeat the failed capability-auth launch experiment.
2. Pin official TinyEMU source and its sample RISC-V Linux image by SHA-256. Add a bounded preparation/build tool with explicit opt-in downloads, safe extraction, local headless builds and no network/SDL/KVM/host filesystem sharing in the test profile. Record observed hashes separately from publisher signatures; do not ship old sample images as a product.
3. First write failing tests for preparation/configuration bounds. Implement only enough to build a host reference emulator on macOS and Linux. At most two compile workers and one 128 MiB guest; use deadlines and guaranteed process cleanup.
4. Run a guest smoke test proving actual Linux, shell conditionals/functions, pipelines, redirection, fork/wait, pseudo-terminals and resize where available. Report unavailable tests honestly. Preserve a bounded transcript and machine-readable evidence; no SSH credentials enter guest artifacts.
5. Compare dependencies and native app embedding surface, then record the next concrete native-port gate. Do not package/deploy a subsystem or change the working SSH/native terminal until runtime and ownership requirements are established.

Acceptance: reproducible host-only build, actual Linux shell test results, resource bounds and cleanup, primary-source provenance and a truthful boundary between guest and PS5 capabilities. Native shell/PTYS and named GUI terminal integrations remain separate unfinished milestones.

## Next boundary: in-process host runtime

The host CLI spike passed. Execute inline in the shared worktree; no subagents,
console changes or commits. Add a separate experimental C adapter, excluded from
the product build. Tests first: repeated real guest boot/destroy, shell/PTY input
and resize, allocation-failure cleanup, bounded queues and invalid inputs. Compile
the pinned interpreter with allocation/fatal hooks into an owned domain, avoiding
the host CLI, file descriptors and process-wide terminal/signal changes. Use fixed
128 MiB guest RAM and a bounded private root-disk copy. Keep all guest invocations
inside a C recovery boundary; never jump through C++ callbacks/destructors. Prove
host behavior and sanitizer results before any native port. This remains trusted
guest research, not a security boundary against malicious guest code.

## Owned worker boundary

Execute inline, still excluded from the product build and with no console
mutation. Preserve virtio console descriptors on output saturation; pause until
the reader makes room. Add a single engine-owning worker with synchronized bounded
I/O, status, resize, cancellation, wake and exactly-once join. Borrow immutable
images only with an explicit lifetime through stop/join. Preserve final output on
natural guest exit. Verify actual guest slow-reader flow, repeated lifecycle,
concurrent stops, startup cancellation, stop while blocked, resize and allocation
failure. Run separate address/undefined/leak and race-detector host checks, then
the PS5 SDK compile/link check. Document host results separately from a future
phat runtime test. Keep the existing distribution unchanged.

## Terminal model adapter

Continue inline. Add an experimental C++ LinuxTerminal owning the tested worker
and VT model, outside the native product build. Each UI pump has fixed byte
budgets in both directions and retains every unaccepted suffix; model-generated
key/paste/mouse and terminal-query replies use the same input path. Resize updates
both model and guest. Stop joins before borrowed image buffers can be released;
restart creates a clean model. Verify a real guest shell through cells/history,
PTY size, terminal replies, Ctrl-C and restart/failure, with host sanitizers and
native compile/link checks. Preserve current renderer/input routing. No console
or product change until this adapter and a reproducible guest are ready.
