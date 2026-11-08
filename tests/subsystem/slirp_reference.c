/* PS5Term GPL-3.0-or-later. Host-only libslirp integration fixture. */
#include "slirp_reference.h"
#include <libslirp.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define QUEUE 32
#define SOCKETS 128
struct Timer
{
    SlirpTimerCb cb;
    void *opaque;
    int used;
    int64_t deadline;
};
struct SlirpReference
{
    Slirp *slirp;
    SlirpCb callbacks;
    unsigned head, count;
    int failed, registered;
    uint8_t frames[QUEUE][PS5_LINUX_ETHERNET_FRAME];
    size_t sizes[QUEUE];
    struct pollfd fds[SOCKETS];
    unsigned nfds;
    struct Timer timers[8];
};
static int64_t clock_ns(void *opaque)
{
    (void)opaque;
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static slirp_ssize_t receive_packet(const void *data, size_t size, void *opaque)
{
    SlirpReference *s = opaque;
    if (size < 14 || size > PS5_LINUX_ETHERNET_FRAME)
    {
        s->failed = 1;
        return -1;
    }
    if (s->count == QUEUE)
        return (slirp_ssize_t)size; /* Ethernet congestion drops. */
    unsigned slot = (s->head + s->count++) % QUEUE;
    memcpy(s->frames[slot], data, size);
    s->sizes[slot] = size;
    return (slirp_ssize_t)size;
}
static void error(const char *msg, void *opaque)
{
    (void)msg;
    ((SlirpReference *)opaque)->failed = 1;
}
static void notify(void *opaque)
{
    (void)opaque;
}
static void registered(slirp_os_socket fd, void *opaque)
{
    (void)fd;
    SlirpReference *s = opaque;
    if (++s->registered > SOCKETS)
        s->failed = 1;
}
static void unregistered(slirp_os_socket fd, void *opaque)
{
    (void)fd;
    SlirpReference *s = opaque;
    if (--s->registered < 0)
        s->failed = 1;
}
static void *timer_new(SlirpTimerCb cb, void *cb_opaque, void *opaque)
{
    SlirpReference *s = opaque;
    for (unsigned i = 0; i < 8; i++)
        if (!s->timers[i].used)
        {
            s->timers[i] = (struct Timer){cb, cb_opaque, 1, -1};
            return &s->timers[i];
        }
    s->failed = 1;
    return NULL;
}
static void timer_free(void *timer, void *opaque)
{
    (void)opaque;
    memset(timer, 0, sizeof(struct Timer));
}
static void timer_mod(void *timer, int64_t ms, void *opaque)
{
    (void)opaque;
    ((struct Timer *)timer)->deadline = ms;
}
static int write_frame(void *opaque, const void *data, size_t size)
{
    SlirpReference *s = opaque;
    if (s->failed || size < 14 || size > PS5_LINUX_ETHERNET_FRAME)
        return -1;
    slirp_input(s->slirp, data, (int)size);
    return s->failed ? -1 : 0;
}
static int read_frame(void *opaque, void *data, size_t capacity)
{
    SlirpReference *s = opaque;
    if (s->failed)
        return -1;
    if (!s->count)
        return 0;
    size_t n = s->sizes[s->head];
    if (n > capacity)
        return -1;
    memcpy(data, s->frames[s->head], n);
    s->head = (s->head + 1) % QUEUE;
    s->count--;
    return (int)n;
}
static int add_poll(slirp_os_socket fd, int events, void *opaque)
{
    SlirpReference *s = opaque;
    if (s->nfds == SOCKETS)
    {
        s->failed = 1;
        return -1;
    }
    short e = 0;
    if (events & SLIRP_POLL_IN)
        e |= POLLIN;
    if (events & SLIRP_POLL_OUT)
        e |= POLLOUT;
    if (events & SLIRP_POLL_PRI)
        e |= POLLPRI;
    unsigned slot = s->nfds++;
    s->fds[slot] = (struct pollfd){fd, e, 0};
    return (int)slot;
}
static int revents(int slot, void *opaque)
{
    SlirpReference *s = opaque;
    if (slot < 0 || (unsigned)slot >= s->nfds)
        return 0;
    short r = s->fds[slot].revents;
    int events = 0;
    if (r & POLLIN)
        events |= SLIRP_POLL_IN;
    if (r & POLLOUT)
        events |= SLIRP_POLL_OUT;
    if (r & POLLPRI)
        events |= SLIRP_POLL_PRI;
    if (r & (POLLERR | POLLNVAL))
        events |= SLIRP_POLL_ERR;
    if (r & POLLHUP)
        events |= SLIRP_POLL_HUP;
    return events;
}
static int poll_network(void *opaque)
{
    SlirpReference *s = opaque;
    if (s->failed)
        return -1;
    s->nfds = 0;
    uint32_t timeout = 0;
    slirp_pollfds_fill_socket(s->slirp, &timeout, add_poll, s);
    if (s->failed)
        return -1;
    int result = poll(s->fds, s->nfds, 0);
    if (result < 0 && errno == EINTR)
        result = 0;
    slirp_pollfds_poll(s->slirp, result < 0, revents, s);
    int64_t now = clock_ns(s) / 1000000;
    for (unsigned i = 0; i < 8; i++)
    {
        struct Timer *t = &s->timers[i];
        if (t->used && t->deadline >= 0 && t->deadline <= now)
        {
            t->deadline = -1;
            t->cb(t->opaque);
        }
    }
    return s->failed ? -1 : 0;
}
SlirpReference *slirp_reference_create(void)
{
    SlirpReference *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    SlirpConfig config = {0};
    config.version = 6;
    config.in_enabled = true;
    config.vnetwork.s_addr = htonl(0x0a000200);
    config.vnetmask.s_addr = htonl(0xffffff00);
    config.vhost.s_addr = htonl(0x0a000202);
    config.vdhcp_start.s_addr = htonl(0x0a00020f);
    config.vnameserver.s_addr = htonl(0x0a000203);
    config.vhostname = "ps5term-linux";
    config.if_mtu = 1500;
    config.if_mru = 1500;
    /* Loopback is allowed ONLY for the local host acceptance servers. */
    config.disable_host_loopback = false;
    config.enable_emu = false;
    SlirpCb cb = {0};
    cb.send_packet = receive_packet;
    cb.guest_error = error;
    cb.clock_get_ns = clock_ns;
    cb.timer_new = timer_new;
    cb.timer_free = timer_free;
    cb.timer_mod = timer_mod;
    cb.notify = notify;
    cb.register_poll_socket = registered;
    cb.unregister_poll_socket = unregistered;
    s->callbacks = cb;
    s->slirp = slirp_new(&config, &s->callbacks, s);
    if (!s->slirp)
    {
        free(s);
        return NULL;
    }
    return s;
}
Ps5LinuxNetwork slirp_reference_device(SlirpReference *s)
{
    return (Ps5LinuxNetwork){s, write_frame, poll_network, read_frame};
}
int slirp_reference_destroy(SlirpReference *s)
{
    if (!s)
        return 0;
    slirp_cleanup(s->slirp);
    int leaked = s->registered;
    free(s);
    return leaked ? -1 : 0;
}
