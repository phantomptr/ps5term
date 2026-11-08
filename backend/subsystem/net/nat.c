/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later
 * Private bounded libslirp profile, serialized C worker only. */
#include "nat.h"
#include "resource.h"
#include "socket_policy.h"
#include <libslirp.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define QUEUE 32
#define SOCKETS PS5_NET_SOCKET_LIMIT
struct Timer {SlirpTimerCb cb;void *opaque;int used;int64_t deadline;};
struct Ps5LinuxNat {
 Ps5NetResources *resources;Ps5NetSockets sockets;Ps5LinuxNatConfig config;
 const void *input;size_t input_size;
 Slirp *slirp;SlirpCb callbacks;unsigned head,count;int failed,registered;
 uint8_t frames[QUEUE][PS5_LINUX_ETHERNET_FRAME];size_t sizes[QUEUE];
 struct pollfd fds[SOCKETS];unsigned nfds;
 struct Timer timers[8];
};
static int64_t clock_ns(void *opaque){(void)opaque;struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)ps5_net_fail();return (int64_t)t.tv_sec*1000000000+t.tv_nsec;}
static slirp_ssize_t receive_packet(const void *data,size_t size,void *opaque){
 Ps5LinuxNat *s=opaque;if(size<14 || size>PS5_LINUX_ETHERNET_FRAME){s->failed=1;return -1;}
 if(s->count==QUEUE)return (slirp_ssize_t)size; /* Ethernet congestion drops. */
 unsigned slot=(s->head+s->count++)%QUEUE;memcpy(s->frames[slot],data,size);s->sizes[slot]=size;return (slirp_ssize_t)size;
}
static void error(const char *msg,void *opaque){(void)msg;((Ps5LinuxNat *)opaque)->failed=1;}
static void notify(void *opaque){(void)opaque;}
static void registered(slirp_os_socket fd,void *opaque){(void)fd;Ps5LinuxNat *s=opaque;if(++s->registered>SOCKETS)s->failed=1;}
static void unregistered(slirp_os_socket fd,void *opaque){(void)fd;Ps5LinuxNat *s=opaque;if(--s->registered<0)s->failed=1;}
static void *timer_new(SlirpTimerCb cb,void *cb_opaque,void *opaque){
 Ps5LinuxNat *s=opaque;for(unsigned i=0;i<8;i++)if(!s->timers[i].used){s->timers[i]=(struct Timer){cb,cb_opaque,1,-1};return &s->timers[i];}ps5_net_fail();
}
static void timer_free(void *timer,void *opaque){(void)opaque;memset(timer,0,sizeof(struct Timer));}
static void timer_mod(void *timer,int64_t ms,void *opaque){(void)opaque;((struct Timer *)timer)->deadline=ms;}
static int guarded(Ps5LinuxNat *s,Ps5NetOperation fn){
 if(s->failed || !ps5_net_sockets_enter(&s->sockets))return 0;
 int ok=ps5_net_guard(s->resources,fn,s);ps5_net_sockets_leave();
 if(!ok)s->failed=1;return ok;
}
static void input_operation(void *opaque){Ps5LinuxNat *s=opaque;slirp_input(s->slirp,s->input,(int)s->input_size);}
static int write_frame(void *opaque,const void *data,size_t size){
 Ps5LinuxNat *s=opaque;if(s->failed || !data || size<14 || size>PS5_LINUX_ETHERNET_FRAME)return -1;
 s->input=data;s->input_size=size;int ok=guarded(s,input_operation);s->input=NULL;s->input_size=0;
 return ok && !s->failed ? 0:-1;
}
static int read_frame(void *opaque,void *data,size_t capacity){
 Ps5LinuxNat *s=opaque;if(s->failed)return -1;if(!s->count)return 0;
 size_t n=s->sizes[s->head];if(n>capacity)return -1;memcpy(data,s->frames[s->head],n);s->head=(s->head+1)%QUEUE;s->count--;return (int)n;
}
static int add_poll(slirp_os_socket fd,int events,void *opaque){
 Ps5LinuxNat *s=opaque;if(s->nfds==SOCKETS){s->failed=1;return -1;}
 short e=0;if(events&SLIRP_POLL_IN)e|=POLLIN;if(events&SLIRP_POLL_OUT)e|=POLLOUT;if(events&SLIRP_POLL_PRI)e|=POLLPRI;
 unsigned slot=s->nfds++;s->fds[slot]=(struct pollfd){fd,e,0};return (int)slot;
}
static int revents(int slot,void *opaque){
 Ps5LinuxNat *s=opaque;if(slot<0 || (unsigned)slot>=s->nfds)return 0;
 short r=s->fds[slot].revents;int events=0;if(r&POLLIN)events|=SLIRP_POLL_IN;if(r&POLLOUT)events|=SLIRP_POLL_OUT;if(r&POLLPRI)events|=SLIRP_POLL_PRI;if(r&(POLLERR|POLLNVAL))events|=SLIRP_POLL_ERR;if(r&POLLHUP)events|=SLIRP_POLL_HUP;return events;
}
static void poll_operation(void *opaque){
 Ps5LinuxNat *s=opaque;s->nfds=0;uint32_t timeout=0;
 slirp_pollfds_fill_socket(s->slirp,&timeout,add_poll,s);if(s->failed)return;
 int result=s->nfds ? poll(s->fds,s->nfds,0):0;
 if(result<0){if(errno!=EINTR){s->failed=1;return;}result=0;}
 slirp_pollfds_poll(s->slirp,0,revents,s);
 int64_t now=clock_ns(s)/1000000;
 for(unsigned i=0;i<8;i++){struct Timer *t=&s->timers[i];if(t->used && t->deadline>=0 && t->deadline<=now){t->deadline=-1;t->cb(t->opaque);}}
}
static int poll_network(void *opaque){Ps5LinuxNat *s=opaque;return guarded(s,poll_operation) && !s->failed ? 0:-1;}
static void create_operation(void *opaque){
 Ps5LinuxNat *s=opaque;
 SlirpConfig config={0};config.version=6;config.in_enabled=true;
 config.vnetwork.s_addr=htonl(0x0a000200);config.vnetmask.s_addr=htonl(0xffffff00);config.vhost.s_addr=htonl(0x0a000202);config.vdhcp_start.s_addr=htonl(0x0a00020f);config.vnameserver.s_addr=htonl(0x0a000203);
 config.vhostname="ps5term-linux";config.if_mtu=1500;config.if_mru=1500;
 config.disable_host_loopback=!s->config.allow_host_loopback;config.enable_emu=false;
 SlirpCb cb={0};cb.send_packet=receive_packet;cb.guest_error=error;cb.clock_get_ns=clock_ns;cb.timer_new=timer_new;cb.timer_free=timer_free;cb.timer_mod=timer_mod;cb.notify=notify;cb.register_poll_socket=registered;cb.unregister_poll_socket=unregistered;
 s->callbacks=cb;s->slirp=slirp_new(&config,&s->callbacks,s);
 if(!s->slirp)ps5_net_fail();
}
Ps5LinuxNat *ps5_linux_nat_create(const Ps5LinuxNatConfig *config){
 if(!config || !config->dns_ipv4 || !config->dns_port || config->memory_limit>4*1024*1024)return NULL;
 Ps5LinuxNat *s=calloc(1,sizeof(*s));if(!s)return NULL;s->config=*config;
 s->sockets.resolver.s_addr=config->dns_ipv4;s->sockets.resolver_port=htons(config->dns_port);
 s->resources=ps5_net_resources_create(config->memory_limit?config->memory_limit:4*1024*1024,8192);
 if(!s->resources){free(s);return NULL;}ps5_net_fail_after(s->resources,config->fail_after);
 if(!guarded(s,create_operation)){ps5_linux_nat_destroy(s);return NULL;}return s;
}
Ps5LinuxNetwork ps5_linux_nat_device(Ps5LinuxNat *s){return (Ps5LinuxNetwork){s,write_frame,poll_network,read_frame};}
static void cleanup_operation(void *opaque){Ps5LinuxNat *s=opaque;slirp_cleanup(s->slirp);s->slirp=NULL;}
void ps5_linux_nat_destroy(Ps5LinuxNat *s){
 if(!s)return;
 /* A partial graph after longjmp is never walked. Independent owners reclaim
  * every descriptor and block even when libslirp cleanup cannot be called. */
 if(s->slirp && !s->failed)guarded(s,cleanup_operation);
 ps5_net_sockets_destroy(&s->sockets);ps5_net_resources_destroy(s->resources);free(s);
}
size_t ps5_linux_nat_memory(const Ps5LinuxNat *s){return s?ps5_net_bytes(s->resources):0;}
size_t ps5_linux_nat_sockets(const Ps5LinuxNat *s){return s?s->sockets.count:0;}
void ps5_linux_nat_fail_after(Ps5LinuxNat *s,size_t count){if(s)ps5_net_fail_after(s->resources,count);}
