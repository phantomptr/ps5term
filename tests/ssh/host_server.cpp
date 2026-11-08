// Loopback-only interoperability fixture. Never deployed. GPL-3.0-or-later.
#include "backend/owned_ssh/connection.hpp"
#include <wolfssh/error.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
using Clock = std::chrono::steady_clock;
struct Client
{
    int fd;
    unsigned reads = 0, writes = 0, budget = 0;
    Clock::time_point started = Clock::now();
    std::unique_ptr<term::ssh::Connection> connection;
    explicit Client(int socket) : fd(socket)
    {
    }
    ~Client()
    {
        connection.reset();
        ::close(fd);
    }
    static int receive(void *opaque, unsigned char *bytes, unsigned size)
    {
        auto &self = *static_cast<Client *>(opaque);
        if (!self.budget || ++self.reads % 7 == 0)
            return WS_CBIO_ERR_WANT_READ;
        const auto count = ::recv(self.fd, bytes, std::min({size, 97U, self.budget}), 0);
        if (count > 0)
        {
            self.budget -= count;
            return static_cast<int>(count);
        }
        if (count == 0)
            return WS_CBIO_ERR_CONN_CLOSE;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return WS_CBIO_ERR_WANT_READ;
        return WS_CBIO_ERR_CONN_RST;
    }
    static int send(void *opaque, const unsigned char *bytes, unsigned size)
    {
        auto &self = *static_cast<Client *>(opaque);
        if (!self.budget || ++self.writes % 5 == 0)
            return WS_CBIO_ERR_WANT_WRITE;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto count = ::send(self.fd, bytes, std::min({size, 113U, self.budget}), flags);
        if (count > 0)
        {
            self.budget -= count;
            return static_cast<int>(count);
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return WS_CBIO_ERR_WANT_WRITE;
        return WS_CBIO_ERR_CONN_RST;
    }
};
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    const char *password = std::getenv("PS5TERM_TEST_PASSWORD");
    if (!password || !*password)
        return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<unsigned char> key((std::istreambuf_iterator<char>(file)), {});
    if (key.empty() || key.size() > 65536 || wolfSSH_Init() != WS_SUCCESS)
        return 2;
    const term::ssh::ServerConfig config{"ps5term", password, argv[2], key};
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return 2;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof address;
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), length) != 0 ||
        getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) != 0 ||
        listen(listener, 4) != 0 || fcntl(listener, F_SETFL, O_NONBLOCK) != 0)
        return 2;
    std::cout << ntohs(address.sin_port) << std::endl;
    std::vector<std::unique_ptr<Client>> clients;
    const auto end = Clock::now() + std::chrono::minutes(5);
    while (Clock::now() < end)
    {
        // A private stdin pipe lets the test owner stop normally, exercising
        // destruction and sanitizer leak checks rather than killing the fixture.
        pollfd control{STDIN_FILENO, POLLIN, 0};
        if (poll(&control, 1, 0) > 0 && (control.revents & (POLLIN | POLLHUP)))
            break;
        const int fd = accept(listener, nullptr, nullptr);
        if (fd >= 0)
        {
            if (clients.size() >= 4 || fcntl(fd, F_SETFL, O_NONBLOCK) != 0)
                ::close(fd);
            else
            {
#ifdef SO_NOSIGPIPE
                int enabled = 1;
                setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled);
#endif
                auto client = std::make_unique<Client>(fd);
                client->connection = std::make_unique<term::ssh::Connection>(
                    config, term::ssh::TransportIO{client.get(), Client::receive, Client::send});
                clients.push_back(std::move(client));
            }
        }
        for (auto &client : clients)
        {
            client->budget = 8192;
            client->connection->tick();
        }
        std::erase_if(clients,
                      [](const auto &client)
                      {
                          if (client->connection->closed() && client->connection->last_error())
                              std::cerr << "connection status " << client->connection->last_error()
                                        << '\n';
                          return client->connection->closed() ||
                                 Clock::now() - client->started > std::chrono::seconds(30);
                      });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    clients.clear();
    ::close(listener);
    wolfSSH_Cleanup();
}
