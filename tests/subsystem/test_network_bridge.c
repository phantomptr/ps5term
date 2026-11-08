/* PS5Term GPL-3.0-or-later. Optional network lifetime/error contract. */
#include "embedded_linux.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned polls, reads, writes;
static int broken_tick, broken_read;
static int send_frame(void *c, const void *b, size_t n)
{
    assert(c && b && n >= 14 && n <= PS5_LINUX_ETHERNET_FRAME);
    writes++;
    return 0;
}
static int poll_network(void *c)
{
    assert(c);
    polls++;
    return broken_tick ? -1 : 0;
}
static int receive_frame(void *c, void *b, size_t n)
{
    assert(c && b && n == PS5_LINUX_ETHERNET_FRAME);
    reads++;
    return broken_read ? (int)n + 1 : 0;
}
static void *load(const char *base, const char *name, size_t *n)
{
    char p[1024];
    assert(snprintf(p, sizeof(p), "%s/%s", base, name) < (int)sizeof(p));
    FILE *f = fopen(p, "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long s = ftell(f);
    assert(s > 0);
    rewind(f);
    void *b = malloc((size_t)s);
    assert(b && fread(b, 1, (size_t)s, f) == (size_t)s);
    fclose(f);
    *n = (size_t)s;
    return b;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    Ps5LinuxImages i = {0};
    i.bios = load(argv[1], "bbl64.bin", &i.bios_size);
    i.kernel = load(argv[1], "kernel-riscv64.bin", &i.kernel_size);
    i.disk = load(argv[1], "root-riscv64.bin", &i.disk_size);
    Ps5Linux *vm = ps5_linux_create(160 * 1024 * 1024);
    assert(vm);
    i.network.context = &polls;
    assert(!ps5_linux_start(vm, &i) && ps5_linux_error(vm) == PS5_LINUX_ARGUMENT);
    i.network = (Ps5LinuxNetwork){&polls, send_frame, poll_network, receive_frame};
    assert(ps5_linux_start(vm, &i) && ps5_linux_network_state(vm) == 1);
    for (int n = 0; n < 10; n++)
        assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING);
    assert(polls == 10 && reads == 10);
    broken_tick = 1;
    assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING && ps5_linux_network_state(vm) == -1);
    unsigned stopped = polls;
    for (int n = 0; n < 10; n++)
        assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING);
    assert(polls == stopped);
    ps5_linux_stop(vm);
    assert(!ps5_linux_network_state(vm));
    broken_tick = 0;
    broken_read = 1;
    assert(ps5_linux_start(vm, &i));
    assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING && ps5_linux_network_state(vm) == -1);
    ps5_linux_stop(vm);
    i.network = (Ps5LinuxNetwork){0};
    assert(ps5_linux_start(vm, &i) && !ps5_linux_network_state(vm));
    stopped = polls;
    assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING && polls == stopped);
    ps5_linux_destroy(vm);
    free((void *)i.bios);
    free((void *)i.kernel);
    free((void *)i.disk);
    puts("PASS: optional NIC validation, poll budget, malformed packet, backend failure, carrier "
         "shutdown, restart and absent-backend isolation");
}
