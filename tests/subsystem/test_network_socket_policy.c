/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later */
#include "net/socket_policy.h"
#include "net/resource.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
struct State
{
    Ps5NetSockets *s;
    int fd;
};
static void make_nonblock(void *opaque)
{
    struct State *s = opaque;
    s->fd = ps5_net_socket(AF_INET, SOCK_DGRAM, 0);
    assert(s->fd >= 0);
    ps5_net_socket_nonblock(s->fd);
    assert(fcntl(s->fd, F_GETFL) & O_NONBLOCK);
    ps5_net_alloc(8192);
}
int main(void)
{
    assert(ps5_net_socket(AF_INET, SOCK_STREAM, 0) < 0 && errno == EINVAL);
    Ps5NetSockets sockets = {0};
    sockets.resolver.s_addr = htonl(0x08080808);
    sockets.resolver_port = htons(53);
    assert(ps5_net_sockets_enter(&sockets) && !ps5_net_sockets_enter(&sockets));
    struct in_addr dns;
    uint16_t port;
    assert(!ps5_net_resolver(&dns, &port) && dns.s_addr == sockets.resolver.s_addr &&
           port == htons(53));
    assert(ps5_net_socket(AF_INET6, SOCK_STREAM, 0) < 0 && errno == EAFNOSUPPORT);
    assert(ps5_net_socket(AF_INET, SOCK_RAW, 0) < 0 && errno == EAFNOSUPPORT);
    int saved[PS5_NET_SOCKET_LIMIT];
    for (size_t i = 0; i < PS5_NET_SOCKET_LIMIT; i++)
    {
        saved[i] = ps5_net_socket(AF_INET, SOCK_DGRAM, 0);
        assert(saved[i] >= 0);
    }
    assert(ps5_net_socket(AF_INET, SOCK_DGRAM, 0) < 0 && errno == EMFILE);
    int foreign = socket(AF_INET, SOCK_DGRAM, 0);
    assert(foreign >= 0);
    assert(ps5_net_socket_close(foreign) < 0 && errno == EBADF && fcntl(foreign, F_GETFD) >= 0);
    close(foreign);
    assert(!ps5_net_socket_close(saved[0]) && sockets.count == PS5_NET_SOCKET_LIMIT - 1);
    assert(ps5_net_socket_close(saved[0]) < 0 && errno == EBADF);
    ps5_net_sockets_leave();
    ps5_net_sockets_destroy(&sockets);
    assert(!sockets.count);
    for (size_t i = 0; i < PS5_NET_SOCKET_LIMIT; i++)
        assert(fcntl(saved[i], F_GETFD) < 0 && errno == EBADF);
    Ps5NetResources *resources = ps5_net_resources_create(256, 8);
    assert(resources);
    struct State state = {&sockets, -1};
    assert(ps5_net_sockets_enter(&sockets));
    assert(!ps5_net_guard(resources, make_nonblock, &state));
    assert(sockets.count == 1);
    ps5_net_sockets_leave();
    ps5_net_sockets_destroy(&sockets);
    ps5_net_resources_destroy(resources);
    assert(fcntl(state.fd, F_GETFD) < 0 && errno == EBADF);
    assert(ps5_net_resolver(&dns, &port) < 0);
    puts("PASS: socket cap, no raw/IPv6, owned close, explicit resolver, nonblocking and "
         "descriptor cleanup after allocation failure");
}
