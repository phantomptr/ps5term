/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TERM_NET_RESOURCE_H
#define PS5TERM_NET_RESOURCE_H
#include <stddef.h>
#include <stdint.h>
/* Worker-owned C-only allocation boundary. Never call across C++ frames or
 * application callbacks. A failed library operation poisons its entire owner;
 * destroy tracked sockets then dispose blocks, never revisit partial graphs. */
typedef struct Ps5NetResources Ps5NetResources;
typedef void (*Ps5NetOperation)(void *);
Ps5NetResources *ps5_net_resources_create(size_t limit, size_t blocks);
void ps5_net_resources_destroy(Ps5NetResources *);
int ps5_net_guard(Ps5NetResources *, Ps5NetOperation, void *);
void *ps5_net_alloc(size_t);
void *ps5_net_calloc(size_t, size_t);
void *ps5_net_realloc(void *, size_t);
void ps5_net_free(void *);
_Noreturn void ps5_net_fail(void);
size_t ps5_net_bytes(const Ps5NetResources *);
size_t ps5_net_blocks(const Ps5NetResources *);
int ps5_net_failed(const Ps5NetResources *);
/* Deterministic test-only fault injection; SIZE_MAX disables. Counts successful
 * allocations/reallocations after this call. No production caller required. */
void ps5_net_fail_after(Ps5NetResources *, size_t);
#endif
