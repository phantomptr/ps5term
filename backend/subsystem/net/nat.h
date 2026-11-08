/* PS5Term SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5TERM_NET_NAT_H
#define PS5TERM_NET_NAT_H
#include "../embedded_linux.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Ps5LinuxNat Ps5LinuxNat;
typedef struct {
 uint32_t dns_ipv4; /* network order, explicit nonzero IPv4 */
 uint16_t dns_port; /* host order, normally 53 */
 int allow_host_loopback; /* false for the app; local fixtures only */
 size_t memory_limit; /* 0 defaults to 4MiB, maximum 4MiB */
 size_t fail_after; /* SIZE_MAX disables test allocation fault injection */
} Ps5LinuxNatConfig;
/* Experimental profile. Not enabled by default or physically verified yet.
 * Own on one serialized worker, destroy after engine join. */
Ps5LinuxNat *ps5_linux_nat_create(const Ps5LinuxNatConfig *);
Ps5LinuxNetwork ps5_linux_nat_device(Ps5LinuxNat *);
void ps5_linux_nat_destroy(Ps5LinuxNat *);
size_t ps5_linux_nat_memory(const Ps5LinuxNat *);
size_t ps5_linux_nat_sockets(const Ps5LinuxNat *);
void ps5_linux_nat_fail_after(Ps5LinuxNat *,size_t);
#ifdef __cplusplus
}
#endif
#endif
