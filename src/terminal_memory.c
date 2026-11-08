/* PS5Term - GPL-3.0-or-later.
 * libvterm 0.3.3 assumes allocator success. Keep its allocations in a tracked,
 * bounded domain and escape allocation failure BEFORE returning a null pointer.
 * x86 compiler builtins avoid an unverified console setjmp import; host ARM
 * uses its standard C runtime (Clang has no ARM builtin implementation).
 * The jump begins and ends in C; it never unwinds a C++ callback or destructor.
 * Only create/input/resize allocate in the pinned engine. All callbacks must
 * return normally and must not call allocating/mutating engine functions.
 * Allocation-free getters/color conversion are safe. Review on engine upgrades.
 */
#include "terminal_memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__clang__) && !defined(__GNUC__)
#error Terminal allocation boundary requires compiler setjmp/longjmp builtins
#endif
#if defined(__x86_64__)
#define TERMINAL_JUMP_BUFFER void *jump[5]
#define TERMINAL_SETJMP(memory) __builtin_setjmp((memory)->jump)
#define TERMINAL_LONGJMP(memory) __builtin_longjmp((memory)->jump, 1)
#else
#include <setjmp.h>
#define TERMINAL_JUMP_BUFFER jmp_buf jump
#define TERMINAL_SETJMP(memory) setjmp((memory)->jump)
#define TERMINAL_LONGJMP(memory) longjmp((memory)->jump, 1)
#endif

typedef union Allocation Allocation;
union Allocation
{
    struct
    {
        Allocation *next, *previous;
        size_t size;
    } link;
    max_align_t alignment;
};
struct TerminalMemory
{
    TERMINAL_JUMP_BUFFER;
    Allocation *allocations;
    size_t used, limit;
    VTerm *vt;
    int failed;
};
static void discard(TerminalMemory *memory)
{
    while (memory->allocations)
    {
        Allocation *block = memory->allocations;
        memory->allocations = block->link.next;
        free(block);
    }
    memory->used = 0;
    memory->vt = NULL;
}
static void *allocate(size_t size, void *opaque)
{
    TerminalMemory *memory = opaque;
    if (size > SIZE_MAX - sizeof(Allocation) ||
        size + sizeof(Allocation) > memory->limit - memory->used)
        TERMINAL_LONGJMP(memory);
    Allocation *block = calloc(1, sizeof(Allocation) + size);
    if (!block)
        TERMINAL_LONGJMP(memory);
    block->link.size = sizeof(Allocation) + size;
    block->link.next = memory->allocations;
    if (block->link.next)
        block->link.next->link.previous = block;
    memory->allocations = block;
    memory->used += block->link.size;
    return block + 1;
}
static void release(void *pointer, void *opaque)
{
    if (!pointer)
        return;
    TerminalMemory *memory = opaque;
    Allocation *block = (Allocation *)pointer - 1;
    if (block->link.previous)
        block->link.previous->link.next = block->link.next;
    else
        memory->allocations = block->link.next;
    if (block->link.next)
        block->link.next->link.previous = block->link.previous;
    memory->used -= block->link.size;
    free(block);
}
static VTermAllocatorFunctions allocator = {allocate, release};
TerminalMemory *terminal_memory_create(int rows, int columns, size_t limit)
{
    TerminalMemory *memory = calloc(1, sizeof(*memory));
    if (!memory)
        return NULL;
    memory->limit = limit;
    if (TERMINAL_SETJMP(memory))
    {
        discard(memory);
        free(memory);
        return NULL;
    }
    memory->vt = vterm_new_with_allocator(rows, columns, &allocator, memory);
    vterm_set_utf8(memory->vt, 1);
    VTermScreen *screen = vterm_obtain_screen(memory->vt);
    vterm_screen_enable_altscreen(screen, 1);
    vterm_screen_reset(screen, 1);
    return memory;
}
void terminal_memory_destroy(TerminalMemory *memory)
{
    if (!memory)
        return;
    /* Pinned engine owns only allocator-backed memory, no external resources.
     * Free tracked blocks directly, including partially initialized graphs. */
    discard(memory);
    free(memory);
}
VTerm *terminal_memory_vterm(TerminalMemory *memory)
{
    return memory && !memory->failed ? memory->vt : NULL;
}
int terminal_memory_feed(TerminalMemory *memory, const char *bytes, size_t size, size_t *consumed)
{
    *consumed = 0;
    if (!memory || memory->failed)
        return 0;
    if (TERMINAL_SETJMP(memory))
    {
        memory->failed = 1;
        discard(memory);
        return 0;
    }
    *consumed = vterm_input_write(memory->vt, bytes, size);
    vterm_screen_flush_damage(vterm_obtain_screen(memory->vt));
    return 1;
}
int terminal_memory_resize(TerminalMemory *memory, int rows, int columns)
{
    if (!memory || memory->failed)
        return 0;
    if (TERMINAL_SETJMP(memory))
    {
        memory->failed = 1;
        discard(memory);
        return 0;
    }
    vterm_set_size(memory->vt, rows, columns);
    vterm_screen_flush_damage(vterm_obtain_screen(memory->vt));
    return 1;
}
