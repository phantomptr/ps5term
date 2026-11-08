// GPL-3.0-or-later. Real listener ownership and restart without console access.
#include "backend/owned_ssh/service.hpp"
#include <cassert>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <array>
int main()
{
    char path[] = "/tmp/ps5term-service-XXXXXX";
    assert(mkdtemp(path));
    const std::string directory = std::string(path) + "/ssh";
    term::ssh::Service server;
    assert(server.start(directory.c_str(), "127.0.0.1", 0));
    const auto port = server.port();
    assert(port && server.listening());
    const std::string password(server.password());
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    // A completed connect can precede accept-queue visibility on macOS.
    for (unsigned i = 1; i <= 100 && !server.clients(); ++i)
    {
        server.tick(i * 1000);
        usleep(1000);
    }
    assert(server.clients() == 1);
    term::ssh::Service conflict;
    assert(!conflict.start(directory.c_str(), "127.0.0.1", port));
    assert(!server.set_password("short"));
    assert(server.clients() == 1 && server.password() == password);
    assert(server.set_password("replacement password"));
    assert(server.clients() == 0 && server.listening() && server.port() == port);
    assert(server.set_password(password));
    server.stop();
    assert(!server.listening() && server.clients() == 0);
    char bytes[1024];
    while (recv(fd, bytes, sizeof bytes, 0) > 0)
    {
    }
    close(fd);
    assert(server.start(directory.c_str(), "127.0.0.1", port));
    assert(server.password() == password);
    std::array<int, 5> peers{};
    unsigned ticks = 0;
    for (auto &peer : peers)
    {
        peer = socket(AF_INET, SOCK_STREAM, 0);
        assert(peer >= 0);
        assert(connect(peer, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
        // Service the accept queue between connects; backlog and client limit differ.
        for (unsigned i = 0; i < 100; ++i)
        {
            server.tick(++ticks * 1000);
            usleep(1000);
        }
    }
    assert(server.clients() == 4); // The fifth connection is never retained.
    // A person gets a minute to log in; silent pre-session clients then expire
    // (simulated time, no wall-clock waits).
    server.tick(30000000);
    assert(server.clients() == 4);
    server.tick(61000000);
    assert(server.clients() == 0 && server.listening());
    for (auto peer : peers)
        close(peer);
    server.stop();
    server.stop();
    unlink((directory + "/host-key.der").c_str());
    unlink((directory + "/password").c_str());
    assert(rmdir(directory.c_str()) == 0 && rmdir(path) == 0);
}
