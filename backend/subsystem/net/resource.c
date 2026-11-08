/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later */
#include "resource.h"
#include <stdlib.h>
#include <string.h>
/* Same recovery boundary as the engine adapter: compiler builtins on x86-64
 * avoid a libc setjmp import; other hosts use the standard functions. */
#if defined(__x86_64__)
#define JUMP_BUFFER void *escape[5]
#define SAVE(r) __builtin_setjmp((r)->escape)
#define ESCAPE(r) __builtin_longjmp((r)->escape,1)
#else
#include <setjmp.h>
#define JUMP_BUFFER jmp_buf escape
#define SAVE(r) setjmp((r)->escape)
#define ESCAPE(r) longjmp((r)->escape,1)
#endif
/* Header alignment also aligns the following payload. All charges include it. */
typedef union Block Block;
union Block {
 struct { Block *next, *prev; size_t size; } h;
 max_align_t alignment;
};
struct Ps5NetResources {
 Block *first;
 size_t used, count, limit, max_blocks, fail_after;
 JUMP_BUFFER;
 int active, failed;
};
#include "thread_context.h"
#define current ((Ps5NetResources *)ps5_net_context_get())
Ps5NetResources *ps5_net_resources_create(size_t limit,size_t blocks) {
 if(!limit || !blocks)return NULL;
 Ps5NetResources *r=calloc(1,sizeof(*r));
 if(r){r->limit=limit;r->max_blocks=blocks;r->fail_after=SIZE_MAX;}
 return r;
}
void ps5_net_resources_destroy(Ps5NetResources *r) {
 if(!r)return;
 /* Destroy is outside the guard; live callbacks must have returned. */
 if(r->active)abort();
 Block *b=r->first;while(b){Block *next=b->h.next;free(b);b=next;}
 free(r);
}
int ps5_net_guard(Ps5NetResources *r,Ps5NetOperation operation,void *arg) {
 if(!r || !operation || current || r->active || r->failed)return 0;
 if(!ps5_net_context_set(r))return 0;r->active=1;
 if(SAVE(r)==0){operation(arg);r->active=0;ps5_net_context_set(NULL);return 1;}
 r->failed=1;r->active=0;ps5_net_context_set(NULL);return 0;
}
_Noreturn void ps5_net_fail(void) {
 if(!current || !current->active)abort(); /* programmer error, never OOM path */
 ESCAPE(current);
}
static Ps5NetResources *owner(void) {
 if(!current || !current->active)abort();return current;
}
static Block *find(void *ptr) {
 Ps5NetResources *r=owner();
 for(Block *b=r->first;b;b=b->h.next)if((void *)(b+1)==ptr)return b;
 ps5_net_fail();
}
static void charge(size_t old,size_t bytes,int new_block) {
 Ps5NetResources *r=owner();
 if(bytes>SIZE_MAX-sizeof(Block) || bytes+sizeof(Block)>r->limit-(r->used-old) ||
    (new_block && r->count==r->max_blocks) || !r->fail_after)ps5_net_fail();
 if(r->fail_after!=SIZE_MAX)r->fail_after--;
}
void *ps5_net_alloc(size_t bytes) {
 Ps5NetResources *r=owner();if(!bytes)bytes=1;charge(0,bytes,1);
 Block *b=malloc(sizeof(*b)+bytes);if(!b)ps5_net_fail();
 b->h.size=bytes;b->h.prev=NULL;b->h.next=r->first;
 if(r->first)r->first->h.prev=b;r->first=b;
 r->used+=sizeof(*b)+bytes;r->count++;return b+1;
}
void *ps5_net_calloc(size_t count,size_t bytes) {
 if(bytes && count>SIZE_MAX/bytes)ps5_net_fail();
 size_t total=count*bytes;void *p=ps5_net_alloc(total);memset(p,0,total);return p;
}
void *ps5_net_realloc(void *ptr,size_t bytes) {
 if(!ptr)return ps5_net_alloc(bytes);
 if(!bytes){ps5_net_free(ptr);return NULL;}
 Ps5NetResources *r=owner();Block *b=find(ptr);size_t old=b->h.size;
 charge(sizeof(*b)+old,bytes,0);
 Block *next=b->h.next,*prev=b->h.prev;
 Block *replacement=realloc(b,sizeof(*b)+bytes);if(!replacement)ps5_net_fail();
 replacement->h.size=bytes;
 if(prev)prev->h.next=replacement;else r->first=replacement;
 if(next)next->h.prev=replacement;
 r->used=r->used-old+bytes;return replacement+1;
}
void ps5_net_free(void *ptr) {
 if(!ptr)return;
 Ps5NetResources *r=owner();Block *b=find(ptr);
 if(b->h.prev)b->h.prev->h.next=b->h.next;else r->first=b->h.next;
 if(b->h.next)b->h.next->h.prev=b->h.prev;
 r->used-=sizeof(*b)+b->h.size;r->count--;free(b);
}
size_t ps5_net_bytes(const Ps5NetResources *r){return r?r->used:0;}
size_t ps5_net_blocks(const Ps5NetResources *r){return r?r->count:0;}
int ps5_net_failed(const Ps5NetResources *r){return !r || r->failed;}
void ps5_net_fail_after(Ps5NetResources *r,size_t count){if(r && !r->active)r->fail_after=count;}
