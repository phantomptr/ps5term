/* PS5Term - GPL-3.0-or-later. Forced include ONLY for pinned TinyEMU C objects. */
#ifndef PS5TERM_TINYEMU_HOOKS_H
#define PS5TERM_TINYEMU_HOOKS_H
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
void *ps5_linux_malloc(size_t size);
void *ps5_linux_calloc(size_t count,size_t size);
void *ps5_linux_realloc(void *pointer,size_t size);
void ps5_linux_free(void *pointer);
char *ps5_linux_strdup(const char *text);
_Noreturn void ps5_linux_exit(int status);
_Noreturn void ps5_linux_abort(void);
_Noreturn void ps5_linux_assert(void);
int ps5_linux_printf(const char *format,...);
int ps5_linux_console_accept(void *device,int size);
#define malloc ps5_linux_malloc
#define calloc ps5_linux_calloc
#define realloc ps5_linux_realloc
#define free ps5_linux_free
#define strdup ps5_linux_strdup
#define exit ps5_linux_exit
#define abort ps5_linux_abort
#define printf(...) ps5_linux_printf(__VA_ARGS__)
#define fprintf(stream, ...) ps5_linux_printf(__VA_ARGS__)
#endif
