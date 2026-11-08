/* PS5Term GPL-3.0-or-later. Real guest against localhost-only network fixtures. */
#include "embedded_linux.h"
#ifdef PS5TERM_BOUNDED_NAT
#include "net/nat.h"
#include <arpa/inet.h>
#else
#include "slirp_reference.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
static void until_state(Ps5Linux *vm, const char *marker, int state)
{
    char output[262144];
    size_t size = 0;
    time_t end = time(NULL) + 30;
    output[0] = 0;
    while (!strstr(output, marker))
    {
        assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING);
        if (state)
            assert(ps5_linux_network_state(vm) == state);
        char b[4096];
        size_t n = ps5_linux_read(vm, b, sizeof(b));
        assert(n < sizeof(output) - size);
        memcpy(output + size, b, n);
        size += n;
        output[size] = 0;
        fwrite(b, 1, n, stdout);
        assert(time(NULL) < end);
    }
}
static void until(Ps5Linux *vm, const char *marker)
{
    until_state(vm, marker, 1);
}
static void command(Ps5Linux *vm, const char *s)
{
    assert(ps5_linux_write(vm, s, strlen(s)) == strlen(s));
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    setvbuf(stdout, NULL, _IONBF, 0);
    Ps5LinuxImages i = {0};
    i.bios = load(argv[1], "bbl64.bin", &i.bios_size);
    i.kernel = load(argv[1], "kernel-riscv64.bin", &i.kernel_size);
    i.disk = load(argv[1], "root-riscv64.bin", &i.disk_size);
    static const uint8_t seed[64] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                     12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
                                     23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    i.rng_seed = seed;
    i.rng_seed_size = sizeof(seed);
    i.wall_clock = 1793800000;
    for (int round = 0; round < 2; round++)
    {
#ifdef PS5TERM_BOUNDED_NAT
        Ps5LinuxNatConfig config = {0};
        config.dns_ipv4 = htonl(INADDR_LOOPBACK);
        config.dns_port = (uint16_t)atoi(argv[3]);
        config.allow_host_loopback = 1;
        config.fail_after = SIZE_MAX;
        Ps5LinuxNat *network = ps5_linux_nat_create(&config);
        assert(network);
        i.network = ps5_linux_nat_device(network);
#else
        SlirpReference *network = slirp_reference_create();
        assert(network);
        i.network = slirp_reference_device(network);
#endif
        Ps5Linux *vm = ps5_linux_create(160 * 1024 * 1024);
        assert(vm && ps5_linux_start(vm, &i));
        until(vm, " # ");
        command(vm, "udhcpc -i eth0 -n -q -t 3 -T 1 && ip -4 addr show eth0 | grep -q '10.0.2.15/' "
                    "&& printf 'NETWORK_%s\\n' DHCP\n");
        until(vm, "NETWORK_DHCP\r\n");
        char script[1024];
#ifdef PS5TERM_BOUNDED_NAT
        snprintf(script, sizeof(script),
                 "nslookup -type=A -retry=1 -timeout=2 ps5term.test 10.0.2.3 | grep -q "
                 "'203.0.113.7' && printf 'NETWORK_%%s\\n' DNS_UDP\n");
#else
        snprintf(script, sizeof(script),
                 "nslookup -type=A -port=%s -retry=1 -timeout=2 ps5term.test 10.0.2.2 | grep -q "
                 "'203.0.113.7' && printf 'NETWORK_%%s\\n' DNS_UDP\n",
                 argv[3]);
#endif
        command(vm, script);
        until(vm, "NETWORK_DNS_UDP\r\n");
#ifdef PS5TERM_BOUNDED_NAT
        /* Real regressions from the phat: resolv.conf must parse without naming a
         * server, the kernel must have entropy at once, and the clock must be set. */
        command(vm, "nslookup -type=A -retry=1 -timeout=2 ps5term.test | grep -q '203.0.113.7' && "
                    "printf 'NETWORK_%s\\n' RESOLV\n");
        until(vm, "NETWORK_RESOLV\r\n");
        command(vm, "test \"$(head -c 16 /dev/random | wc -c)\" = 16 && test \"$(date +%s)\" -ge "
                    "1793800000 && printf 'BOOT_%s\\n' SEEDED\n");
        until(vm, "BOOT_SEEDED\r\n");
#endif
        snprintf(script, sizeof(script),
                 "wget -T 3 -qO- http://10.0.2.2:%s/proof | grep -q 'ps5term-network-proof' && "
                 "printf 'NETWORK_%%s\\n' HTTP_TCP\n",
                 argv[2]);
        command(vm, script);
        until(vm, "NETWORK_HTTP_TCP\r\n");
#ifdef PS5TERM_BOUNDED_NAT
        /* Force a new TCP socket allocation to fail. Carrier must drop, the guest
         * must remain responsive, and independent owners must still close sockets. */
        ps5_linux_nat_fail_after(network, 0);
        snprintf(script, sizeof(script),
                 "wget -T 1 -qO- http://10.0.2.2:%s/proof; sleep 1; printf "
                 "'NETWORK_CARRIER_%%s\\n' \"$(cat /sys/class/net/eth0/carrier)\"\n",
                 argv[2]);
        command(vm, script);
        until_state(vm, "NETWORK_CARRIER_0\r\n", 0);
        assert(ps5_linux_network_state(vm) == -1);
        command(vm, "printf 'NETWORK_%s\\n' STOP\n");
        until_state(vm, "NETWORK_STOP\r\n", -1);
#else
        command(vm, "printf 'NETWORK_%s\\n' STOP\n");
        until(vm, "NETWORK_STOP\r\n");
#endif
        ps5_linux_destroy(vm);
#ifdef PS5TERM_BOUNDED_NAT
        assert(ps5_linux_nat_memory(network) <= 4 * 1024 * 1024 &&
               ps5_linux_nat_sockets(network) <= 64);
        ps5_linux_nat_destroy(network);
#else
        assert(!slirp_reference_destroy(network));
#endif
    }
    free((void *)i.bios);
    free((void *)i.kernel);
    free((void *)i.disk);
    puts("PASS: real guest DHCP, UDP DNS, TCP HTTP, two boots and socket cleanup (host test; not "
         "hardware proof)");
}
