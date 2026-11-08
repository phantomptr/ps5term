// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
static std::size_t mapped_size, unmapped_size;
static int result;
extern "C" int sceKernelMapNamedFlexibleMemory(void **out, std::size_t size, int prot, int flags,
                                               const char *)
{
    assert(prot == 3 && flags == 0);
    mapped_size = size;
    if (result)
        return result;
    *out = std::malloc(size);
    return 0;
}
extern "C" int sceKernelMunmap(void *p, std::size_t size)
{
    unmapped_size = size;
    std::free(p);
    return 0;
}
#include "guest_memory.h"
int main()
{
    const auto size = 1024 * 1024 + 17;
    void *p = ps5_guest_buffer_alloc(size);
    assert(p && mapped_size == 1024 * 1024 + 16384);
    ps5_guest_buffer_free(p, size);
    assert(unmapped_size == mapped_size);
    result = -1;
    errno = 0;
    assert(!ps5_guest_buffer_alloc(size) && errno == ENOMEM);
    assert(!ps5_guest_buffer_alloc(std::numeric_limits<std::size_t>::max()));
    p = ps5_guest_buffer_alloc(23);
    assert(p);
    ps5_guest_buffer_free(p, 23);
}
