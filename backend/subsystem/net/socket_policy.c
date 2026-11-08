/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later
 * Host implementation of the owned socket boundary. Native transport still
 * requires SDK link and physical-device acceptance before app integration. */
#include "socket_policy.h"
#include "resource.h"
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "thread_context.h"
#define current ((Ps5NetSockets *)ps5_net_context_get())
int ps5_net_sockets_enter(Ps5NetSockets *s){if(!s || current)return 0;return ps5_net_context_set(s);}
void ps5_net_sockets_leave(void){ps5_net_context_set(NULL);}
static int slot(int fd){if(!current)return -1;for(size_t i=0;i<current->count;i++)if(current->fds[i]==fd)return (int)i;return -1;}
int ps5_net_socket(int domain,int type,int protocol){
 if(!current){errno=EINVAL;return -1;}
 if(domain!=AF_INET || (type!=SOCK_STREAM && type!=SOCK_DGRAM) ||
    (protocol!=0 && protocol!=IPPROTO_TCP && protocol!=IPPROTO_UDP)){errno=EAFNOSUPPORT;return -1;}
 if(current->count==PS5_NET_SOCKET_LIMIT){errno=EMFILE;return -1;}
 int fd=socket(domain,type,protocol);if(fd>=0)current->fds[current->count++]=fd;return fd;
}
int ps5_net_socket_close(int fd){
 int index=slot(fd);if(index<0){errno=EBADF;return -1;}
 current->fds[index]=current->fds[--current->count];return close(fd);
}
void ps5_net_socket_nonblock(int fd){
 if(slot(fd)<0)ps5_net_fail();
#if defined(PS5TERM_NATIVE_NET)
 /* SceNet-style nonblocking option, as the app's existing listeners use. A
  * reset peer must never raise SIGPIPE in the application process. */
 int enabled=1;
 if(setsockopt(fd,SOL_SOCKET,0x1200,&enabled,sizeof(enabled))!=0)ps5_net_fail();
 (void)setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof(enabled)); /* Best effort, as the SSH service. */
#else
 int flags=fcntl(fd,F_GETFL);
 if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)!=0 || fcntl(fd,F_SETFD,FD_CLOEXEC)!=0)ps5_net_fail();
#ifdef SO_NOSIGPIPE
 int enabled=1;if(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof(enabled))!=0)ps5_net_fail();
#endif
#endif
}
int ps5_net_ioctl(int fd,unsigned long request,void *value){
 if(slot(fd)<0){errno=EBADF;return -1;}
 if(ioctl(fd,request,value)==0)return 0;
 if(request==FIONREAD && value){*(int *)value=PS5_NET_DATAGRAM_DEFAULT;return 0;}
 return -1;
}
void ps5_net_sockets_destroy(Ps5NetSockets *s){if(!s)return;while(s->count)close(s->fds[--s->count]);}
int ps5_net_resolver(struct in_addr *address,uint16_t *port){
 if(!current || !current->resolver.s_addr || !current->resolver_port)return -1;
 *address=current->resolver;*port=current->resolver_port;return 0;
}

uint32_t ps5_net_random_uniform(uint32_t upper){
 if(upper<2)return 0;
#if defined(PS5TERM_NATIVE_NET)
 extern int ps5term_native_seed(unsigned char *,unsigned int);
 /* Rejection sampling, bounded attempts; failure tears down only networking. */
 uint32_t minimum=(uint32_t)(-upper)%upper;
 for(unsigned i=0;i<16;i++){
  uint32_t value;
  if(ps5term_native_seed((unsigned char *)&value,sizeof(value))!=0)ps5_net_fail();
  if(value>=minimum)return value%upper;
 }
 ps5_net_fail();
#else
 return arc4random_uniform(upper);
#endif
}
