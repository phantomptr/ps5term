# Embedded Linux outbound network

Goal: guest DNS/TCP/UDP/HTTP through PS5Term, no helper payload or public guest listener.

Use a bounded virtio Ethernet bridge with an explicit optional packet backend,
owned on the existing worker. Start with maintained libslirp4.9.7 (BSD-3-Clause),
IPv4 10.0.2.0/24, DHCP/DNS, MTU1500. Disable host forwarding, TFTP, SMB/process
spawn and IPv6. The app's native SSH service remains separate.

1. Test/implement the engine Ethernet bridge using fixed packet queues and
   validated callback sizes. Optional zero context preserves current no-NIC build.
   No reentry or extra threads; all callbacks run within the serialized engine.
2. Host libslirp reference adapter and actual guest DNS/UDP/TCP/HTTP tests against
   bounded local fixtures; no Internet test dependency. Test offline/error/stop,
   descriptor cleanup and packet backpressure. Guest kernel needs VIRTIO_NET,
   DHCP/IP overlay, and no misleading startup message when NIC absent.
3. Port pinned source to PS5 sockets. SceNet FDs cannot use POSIX fcntl/close/poll.
   Explicit resolver config, checked nonblocking sockets, fixed descriptor table,
   allocator cap/OOM cleanup and no process-launch paths. Preserve full notices.
4. SDK build and phat-only guarded normal update/HomeCross acceptance. Only then
   default-enable networking and expose truthful Settings status. Other firmware
   remains untested. Never use the old TinyEMU2019 bundled SLiRP implicitly.

Official observed source: https://qemu.googlesource.com/libslirp/+/refs/tags/v4.9.7
commit ce12dcddf710a71dc0de6450ec95edbadf8e9f66, observed archive SHA256
6b19073be7764feabfaa55543b4567577b1a004dae7bbfc405a384f36fedaf3c.
No release-signature claim. GLib dependency requires deliberate native treatment;
platform must not terminate the whole app on network allocation failure.

Status 2026-11-04: all four steps complete and enabled by default. Phat 13.60 passed DHCP,
DNS, HTTP, HTTPS and stop/restart cleanup. The first native run found a stack overflow no
host could show: libslirp keeps one frame on the stack, sized by IF_MTU_MAX, so the
profile now uses a 1500-byte limit and the worker gets an explicit 1 MiB stack.
