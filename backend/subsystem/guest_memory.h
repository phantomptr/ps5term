/* PS5Term - GPL-3.0-or-later. Large guest buffers do not use the native libc heap.
 * Ordinary private, non-executable mappings only. Caller owns size and lifetime.
 */
#ifndef PS5TERM_GUEST_MEMORY_H
#define PS5TERM_GUEST_MEMORY_H
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "guest_trace.h"
#if defined(__PROSPERO__) || defined(PS5TERM_NATIVE_MEMORY_TEST)
#ifdef __cplusplus
extern "C" {
#endif
int sceKernelMapNamedFlexibleMemory(void **,size_t,int,int,const char *);
int sceKernelMunmap(void *,size_t);
#ifdef __cplusplus
}
#endif
#elif defined(PS5TERM_GUEST_MMAP_TEST)
#include <sys/mman.h>
#endif
static inline size_t ps5_guest_mapping_size(size_t size)
{
    return size>SIZE_MAX-16383 ? 0 : (size+16383)&~(size_t)16383;
}
static inline void *ps5_guest_buffer_alloc(size_t size)
{
#if defined(__PROSPERO__) || defined(PS5TERM_NATIVE_MEMORY_TEST)
    if(size >= 1024 * 1024) {
        size_t rounded=ps5_guest_mapping_size(size);
        if(!rounded) { errno=ENOMEM;return NULL; }
        void *p=NULL;
        ps5_guest_trace("native-map-request",rounded);
        int rc=sceKernelMapNamedFlexibleMemory(&p,rounded,3,0,"PS5TermLinux");
        ps5_guest_trace(rc ? "native-map-failed" : "native-map-ready",rounded);
        if(rc) { errno=ENOMEM;return NULL; }
        return p;
    }
#elif defined(PS5TERM_GUEST_MMAP_TEST)
    if(size >= 1024 * 1024) {
        ps5_guest_trace("map-request",size);
        void *p=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
        ps5_guest_trace(p==MAP_FAILED ? "map-failed" : "map-ready",size);
        return p==MAP_FAILED ? NULL : p;
    }
#endif
    return malloc(size);
}
static inline void ps5_guest_buffer_free(void *pointer,size_t size)
{
#if defined(__PROSPERO__) || defined(PS5TERM_NATIVE_MEMORY_TEST)
    if(pointer && size >= 1024 * 1024) { (void)sceKernelMunmap(pointer,ps5_guest_mapping_size(size));return; }
#elif defined(PS5TERM_GUEST_MMAP_TEST)
    if(pointer && size >= 1024 * 1024) { (void)munmap(pointer,size); return; }
#else
    (void)size;
#endif
    free(pointer);
}
#endif
