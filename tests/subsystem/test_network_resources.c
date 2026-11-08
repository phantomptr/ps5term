/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later */
#include "net/resource.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct State
{
    Ps5NetResources *r;
    void *p;
    int nested;
};
static void allocate(void *arg)
{
    struct State *s = arg;
    s->p = ps5_net_alloc(63);
    assert((uintptr_t)s->p % _Alignof(max_align_t) == 0);
    memset(s->p, 0xa5, 63);
    s->p = ps5_net_realloc(s->p, 129);
    for (size_t i = 0; i < 63; i++)
        assert(((unsigned char *)s->p)[i] == 0xa5);
    assert(ps5_net_bytes(s->r) >= 129 && ps5_net_blocks(s->r) == 1);
    s->p = ps5_net_realloc(s->p, 12);
    for (size_t i = 0; i < 12; i++)
        assert(((unsigned char *)s->p)[i] == 0xa5);
    ps5_net_free(s->p);
    s->p = ps5_net_calloc(4, 9);
    for (size_t i = 0; i < 36; i++)
        assert(((unsigned char *)s->p)[i] == 0);
    ps5_net_free(s->p);
    s->p = NULL;
}
static void overflow(void *arg)
{
    (void)arg;
    ps5_net_calloc(SIZE_MAX, 2);
    assert(0);
}
static void exhaust(void *arg)
{
    (void)arg;
    for (;;)
        ps5_net_alloc(31);
}
static void recurse(void *arg)
{
    struct State *s = arg;
    s->nested = ps5_net_guard(s->r, allocate, s);
}
static void wrong_owner(void *arg)
{
    struct State *s = arg;
    ps5_net_free(s->p);
    assert(0);
}
static void retain(void *arg)
{
    struct State *s = arg;
    s->p = ps5_net_alloc(32);
}
int main(void)
{
    assert(!ps5_net_resources_create(0, 1));
    assert(!ps5_net_resources_create(1024, 0));
    for (size_t fault = 0; fault < 5; fault++)
    {
        Ps5NetResources *r = ps5_net_resources_create(4096, 8);
        assert(r);
        struct State s = {r, NULL, 0};
        ps5_net_fail_after(r, fault);
        int ok = ps5_net_guard(r, allocate, &s);
        assert(ok == (fault == 4));
        assert(ps5_net_failed(r) == !ok);
        if (!ok)
            assert(!ps5_net_guard(r, allocate, &s));
        assert(ps5_net_bytes(r) <= 4096 && ps5_net_blocks(r) <= 8);
        ps5_net_resources_destroy(r);
    }
    Ps5NetResources *r = ps5_net_resources_create(256, 2);
    struct State s = {r, NULL, 1};
    assert(ps5_net_guard(r, recurse, &s) && !s.nested && !ps5_net_failed(r));
    assert(!ps5_net_guard(r, exhaust, NULL));
    assert(ps5_net_blocks(r) <= 2 && ps5_net_bytes(r) <= 256);
    ps5_net_resources_destroy(r);
    r = ps5_net_resources_create(4096, 8);
    assert(!ps5_net_guard(r, overflow, NULL));
    ps5_net_resources_destroy(r);
    r = ps5_net_resources_create(4096, 8);
    s.r = r;
    assert(ps5_net_guard(r, retain, &s));
    Ps5NetResources *other = ps5_net_resources_create(4096, 8);
    assert(!ps5_net_guard(other, wrong_owner, &s));
    assert(ps5_net_blocks(r) == 1);
    ps5_net_resources_destroy(other);
    ps5_net_resources_destroy(r);
    puts("PASS: bounded network allocations, overflow, alignment, realloc retention, all fault "
         "cutoffs, poisoned owner, nested guard and owner isolation");
}
