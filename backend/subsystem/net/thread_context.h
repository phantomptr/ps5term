/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later
 * Private per-translation-unit worker context. pthread keys avoid the PS5
 * toolchain's unavailable __emutls_get_address runtime. */
#ifndef PS5TERM_NET_THREAD_CONTEXT_H
#define PS5TERM_NET_THREAD_CONTEXT_H
#include <pthread.h>
static pthread_key_t ps5_net_context_key;
static pthread_once_t ps5_net_context_once=PTHREAD_ONCE_INIT;
static int ps5_net_context_ready;
static void ps5_net_context_init(void){ps5_net_context_ready=pthread_key_create(&ps5_net_context_key,NULL)==0;}
static void *ps5_net_context_get(void){
 if(pthread_once(&ps5_net_context_once,ps5_net_context_init)!=0 || !ps5_net_context_ready)return NULL;
 return pthread_getspecific(ps5_net_context_key);
}
static int ps5_net_context_set(void *value){
 if(pthread_once(&ps5_net_context_once,ps5_net_context_init)!=0 || !ps5_net_context_ready)return 0;
 return pthread_setspecific(ps5_net_context_key,value)==0;
}
#endif
