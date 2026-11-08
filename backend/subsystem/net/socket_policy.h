/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TERM_NET_SOCKET_POLICY_H
#define PS5TERM_NET_SOCKET_POLICY_H
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#define PS5_NET_SOCKET_LIMIT 64
typedef struct {
 int fds[PS5_NET_SOCKET_LIMIT];
 size_t count;
 struct in_addr resolver;
 uint16_t resolver_port; /* network byte order */
} Ps5NetSockets;
/* Same serialized C worker as the allocation guard. No reentry. */
int ps5_net_sockets_enter(Ps5NetSockets *);
void ps5_net_sockets_leave(void);
void ps5_net_sockets_destroy(Ps5NetSockets *);
int ps5_net_socket(int domain,int type,int protocol);
int ps5_net_socket_close(int fd);
void ps5_net_socket_nonblock(int fd);
/* Owned-descriptor ioctl. FIONREAD falls back to a bounded datagram size when
 * the socket layer rejects it, instead of silently dropping every datagram. */
#define PS5_NET_DATAGRAM_DEFAULT 8192
int ps5_net_ioctl(int fd,unsigned long request,void *value);
uint32_t ps5_net_random_uniform(uint32_t);
int ps5_net_resolver(struct in_addr *,uint16_t *);
#endif
