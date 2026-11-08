/* PS5Term - GPL-3.0-or-later. Internal libvterm allocation boundary. */
#pragma once
#include <stddef.h>
#include "../third_party/libvterm/include/vterm.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct TerminalMemory TerminalMemory;
    TerminalMemory *terminal_memory_create(int rows, int columns, size_t limit);
    void terminal_memory_destroy(TerminalMemory *memory);
    VTerm *terminal_memory_vterm(TerminalMemory *memory);
    int terminal_memory_feed(TerminalMemory *memory, const char *bytes, size_t size,
                             size_t *consumed);
    int terminal_memory_resize(TerminalMemory *memory, int rows, int columns);
#ifdef __cplusplus
}
#endif
