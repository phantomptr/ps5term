// PS5Term - native network setup shared by internal services.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
namespace term
{
inline bool nonblocking_socket(int fd) noexcept
{
#if defined(__FreeBSD__)
    // SceNet descriptors are not kernel file descriptors. F_SETFL is not the
    // SceNet nonblocking interface (see ProsperoStore's curl socket setup).
    int enabled = 1;
    return ::setsockopt(fd, SOL_SOCKET, 0x1200, &enabled, sizeof(enabled)) == 0;
#else
    return ::fcntl(fd, F_SETFL, O_NONBLOCK) == 0;
#endif
}
inline sockaddr_in loopback_address(unsigned port) noexcept
{
    sockaddr_in address{};
#if defined(__FreeBSD__) || defined(__APPLE__)
    address.sin_len = sizeof(address);
#endif
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}
} // namespace term
