/* PS5Term GPL-3.0-or-later. Host-only reference; NOT a PS5 networking port.
 * Uses installed libslirp4.9.7/GLib. Library allocations are not app-capped.
 * Test fixtures explicitly allow host loopback. Never linked into the app. */
#ifndef PS5TERM_SLIRP_REFERENCE_H
#define PS5TERM_SLIRP_REFERENCE_H
#include "embedded_linux.h"
typedef struct SlirpReference SlirpReference;
SlirpReference *slirp_reference_create(void);
Ps5LinuxNetwork slirp_reference_device(SlirpReference *);
int slirp_reference_destroy(SlirpReference *); /* 0 all registered sockets closed */
#endif
