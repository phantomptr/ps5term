// PS5Term - real socket startup/identity tests. SPDX-License-Identifier: GPL-3.0-or-later
#include "ssh_service.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
static int listener(unsigned &port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    socklen_t n = sizeof(address);
    assert(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &n) == 0);
    port = ntohs(address.sin_port);
    assert(listen(fd, 8) == 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
int main()
{
    unsigned port = 0, other = 0, loader_port = 0;
    int server = listener(port), unused = listener(other), loader = listener(loader_port);
    close(unused); // Refused connections exercise fallback/startup.
    term::SshService service("/missing", port, other, loader_port);
    int peer = -1;
    for (unsigned i = 0; i < 200 && !service.listening(); ++i)
    {
        service.tick(i * 10000);
        if (peer < 0)
            peer = accept(server, nullptr, nullptr);
        if (peer >= 0)
        {
            const char *text = "SSH-2.0-PS5Term_1.0\r\n";
            // Deliberately fragment the identification line.
            if (i == 1)
                assert(send(peer, text, 5, 0) == 5);
            if (i == 2)
                assert(send(peer, text + 5, 16, 0) == 16);
        }
        usleep(1000);
    }
    assert(service.listening());
    assert(service.port() == port);
    assert(!service.uploaded());
    close(peer);
    close(server);

    // A generic SSH server is not reported as our bundled service.
    server = listener(port);
    term::SshService foreign("/missing", port, other, loader_port);
    peer = -1;
    for (unsigned i = 0; i < 200 && !foreign.failed(); ++i)
    {
        foreign.tick(i * 100000);
        if (peer < 0)
            peer = accept(server, nullptr, nullptr);
        if (peer >= 0)
        {
            send(peer, "SSH-2.0-OpenSSH_9.0\r\n", 21, 0);
            close(peer);
            peer = -2;
        }
        // Accept the loader connection so the missing internal asset is detected.
        int pending = accept(loader, nullptr, nullptr);
        if (pending >= 0)
            close(pending);
        usleep(1000);
    }
    assert(!foreign.listening());
    assert(foreign.failed());
    assert(!foreign.uploaded());
    close(server);
    close(loader);

    // Stream a payload larger than one buffer through a real local loader peer.
    char name[] = "/tmp/ps5term-service-XXXXXX";
    int file = mkstemp(name);
    assert(file >= 0);
    std::string payload(25000, 'x');
    assert(write(file, payload.data(), payload.size()) == static_cast<ssize_t>(payload.size()));
    close(file);
    loader = listener(loader_port);
    server = listener(port);
    close(server);
    term::SshService startup(name, port, other, loader_port);
    peer = -1;
    std::string received;
    for (unsigned i = 0; i < 500 && !startup.failed(); ++i)
    {
        startup.tick(i * 10000);
        if (peer < 0)
        {
            peer = accept(loader, nullptr, nullptr);
            if (peer >= 0)
                fcntl(peer, F_SETFL, O_NONBLOCK);
        }
        if (peer >= 0)
        {
            char buffer[1024];
            auto n = recv(peer, buffer, sizeof(buffer), 0);
            if (n > 0)
            {
                received.append(buffer, n);
                // A retry keypress must not truncate an ELF already being sent.
                if (received.size() == 1024)
                    startup.retry();
            }
            else if (n == 0)
                break;
        }
        usleep(1000);
    }
    assert(received == payload);
    assert(startup.uploaded());
    close(peer);
    close(loader);
    unlink(name);
    std::puts("PASS: fragmented SSH identity, foreign listener refusal, missing asset failure, "
              "partial payload upload");
}
