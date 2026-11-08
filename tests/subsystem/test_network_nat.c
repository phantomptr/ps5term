/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later */
#include "net/nat.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
static unsigned char arp[42] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 2, 0x50, 0x53, 0x35, 0,  1, 8, 6, 0, 1, 8, 0,  6, 4, 0,
    1,    2,    0x50, 0x53, 0x35, 0,    1, 10,   0,    2,    15, 0, 0, 0, 0, 0, 0, 10, 0, 2, 2};
int main(void)
{
    assert(!ps5_linux_nat_create(NULL));
    Ps5LinuxNatConfig c = {0};
    assert(!ps5_linux_nat_create(&c));
    c.dns_ipv4 = htonl(0x08080808);
    c.dns_port = 53;
    c.fail_after = SIZE_MAX;
    c.memory_limit = 4 * 1024 * 1024 + 1;
    assert(!ps5_linux_nat_create(&c));
    c.memory_limit = 0;
    size_t first_success = SIZE_MAX;
    for (size_t i = 0; i < 64; i++)
    {
        c.fail_after = i;
        Ps5LinuxNat *n = ps5_linux_nat_create(&c);
        if (n)
        {
            first_success = i;
            ps5_linux_nat_destroy(n);
            break;
        }
    }
    assert(first_success > 0 && first_success < 64);
    c.fail_after = SIZE_MAX;
    for (size_t i = 0; i < 16; i++)
    {
        Ps5LinuxNat *n = ps5_linux_nat_create(&c);
        assert(n);
        Ps5LinuxNetwork dev = ps5_linux_nat_device(n);
        ps5_linux_nat_fail_after(n, i);
        int ok = dev.write(dev.context, arp, sizeof(arp));
        if (ok == 0)
        {
            unsigned char frame[1518];
            assert(dev.poll(dev.context) == 0);
            int size = dev.read(dev.context, frame, sizeof(frame));
            assert(size >= 42 && frame[20] == 0 && frame[21] == 2);
        }
        else
            assert(dev.poll(dev.context) < 0);
        assert(ps5_linux_nat_memory(n) <= 4 * 1024 * 1024 && ps5_linux_nat_sockets(n) <= 64);
        ps5_linux_nat_destroy(n);
    }
    printf("PASS: real bounded libslirp construction cutoffs (%zu), ARP, runtime allocation "
           "failures and clean destroy\n",
           first_success);
}
