/* PS5Term - GPL-3.0-or-later. Experimental; not part of the app build. */
#ifndef PS5TERM_EMBEDDED_LINUX_H
#define PS5TERM_EMBEDDED_LINUX_H
#include <stddef.h>
#include <stdint.h>
#include "shared_folder.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PS5_LINUX_MAX_HEAP ((size_t)192*1024*1024)
#define PS5_LINUX_INPUT_CAPACITY 16384
#define PS5_LINUX_OUTPUT_CAPACITY 65536
#define PS5_LINUX_ETHERNET_FRAME 1518
typedef struct Ps5Linux Ps5Linux;
typedef enum { PS5_LINUX_STOPPED, PS5_LINUX_RUNNING, PS5_LINUX_FAILED } Ps5LinuxState;
typedef enum { PS5_LINUX_OK, PS5_LINUX_ARGUMENT, PS5_LINUX_MEMORY,
               PS5_LINUX_ENGINE, PS5_LINUX_OUTPUT_FULL, PS5_LINUX_BUSY } Ps5LinuxError;
/* Optional second disk. Synchronous callbacks run only on the engine owner;
 * return 0 on complete success, -1 on failure. Sector/count units are 512 bytes.
 * Callbacks must not reenter the engine, throw/longjmp, or claim asynchronous IO.
 * Context remains alive until the worker has joined. Zero sectors disables it.
 * A writable backend must make acknowledged writes durable: virtio advertises
 * no writeback cache/flush feature. No ownership of the context is transferred. */
typedef struct {
    void *context;
    uint64_t sectors;
    int (*read)(void *,uint64_t,void *,size_t);
    int (*write)(void *,uint64_t,const void *,size_t);
} Ps5LinuxDisk;
/* Optional borrowed Ethernet backend, worker-only until join. No reentry,
 * exceptions or longjmp. write accepts a complete frame (0 success, -1 error);
 * poll is nonblocking (0 success, -1 error); read returns 0 empty, frame length,
 * or -1 error. It must never write beyond capacity. Frames are 14..1518 bytes.
 * An error drops carrier and stops callbacks until restart, not the guest.
 * The platform owns backend queues/sockets and releases them after join. */
typedef struct {
    void *context;
    int (*write)(void *,const void *,size_t);
    int (*poll)(void *);
    int (*read)(void *,void *,size_t);
} Ps5LinuxNetwork;
typedef struct {
    const uint8_t *bios; size_t bios_size;
    const uint8_t *kernel; size_t kernel_size;
    const uint8_t *disk; size_t disk_size;
    Ps5LinuxDisk data_disk;
    Ps5LinuxShare shared_folder;
    Ps5LinuxNetwork network;
    /* Optional boot entropy (at most 64 bytes, copied at start): the guest kernel
     * credits it, so getrandom and TLS do not stall on an idle interpreted CPU. */
    const uint8_t *rng_seed; size_t rng_seed_size;
    /* Optional wall clock (seconds since 1970) for the guest; 0 means unknown.
     * TLS certificate validation needs it; the guest has no battery RTC. */
    int64_t wall_clock;
    /* Nonzero adds the second virtio console for the guest agent. */
    int agent;
} Ps5LinuxImages;
/* Single-owner API: caller serializes ALL calls on an instance. No threads,
 * callbacks into application code, file descriptors, signal handlers or host
 * terminal state are created. Distinct instances may not execute concurrently;
 * engine entry rejects overlap. Worker/renderer synchronization is a later layer.
 * Heap cap includes allocation metadata, RAM and a private writable disk copy;
 * fixed owner/queue storage (~82 KiB) is additional. Guest RAM is 128 MiB.
 * BIOS/kernel buffers are borrowed only during start; disk is copied.
 * Trusted images only: upstream interpreter is NOT a malicious-guest sandbox.
 */
Ps5Linux *ps5_linux_create(size_t heap_limit);
void ps5_linux_destroy(Ps5Linux *vm);
int ps5_linux_start(Ps5Linux *vm,const Ps5LinuxImages *images);
void ps5_linux_stop(Ps5Linux *vm);
/* Executes 1..50000 guest cycles per call. Invalid budgets do no work. */
Ps5LinuxState ps5_linux_step(Ps5Linux *vm,unsigned cycles);
size_t ps5_linux_write(Ps5Linux *vm,const void *bytes,size_t size);
size_t ps5_linux_read(Ps5Linux *vm,void *bytes,size_t size);
void ps5_linux_discard_input(Ps5Linux *vm);
unsigned long ps5_linux_data_writes(const Ps5Linux *vm);
int ps5_linux_resize(Ps5Linux *vm,unsigned columns,unsigned rows);
Ps5LinuxState ps5_linux_state(const Ps5Linux *vm);
Ps5LinuxError ps5_linux_error(const Ps5Linux *vm);
size_t ps5_linux_heap_used(const Ps5Linux *vm);
int ps5_linux_output_blocked(const Ps5Linux *vm);
unsigned ps5_linux_sleep_hint(const Ps5Linux *vm);
/* 0 no NIC/stopped, 1 backend active, -1 backend failed (guest still running). */
int ps5_linux_network_state(const Ps5Linux *vm);
/* Second byte stream (hvc1), only with Ps5LinuxImages.agent set. Same flow control
 * as the console, but a full queue holds back only this stream, not the guest. */
size_t ps5_linux_agent_write(Ps5Linux *vm,const void *bytes,size_t size);
size_t ps5_linux_agent_read(Ps5Linux *vm,void *bytes,size_t size);
#ifdef PS5TERM_SUBSYSTEM_TESTING
int ps5_linux_test_allocator(Ps5Linux *vm);
void ps5_linux_test_fail_after(Ps5Linux *vm,size_t successful_allocations);
#endif
#ifdef __cplusplus
}
#endif
#endif
