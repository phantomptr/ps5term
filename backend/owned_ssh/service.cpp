// PS5Term owned SSH service. GPL-3.0-or-later.
#include "service.hpp"
#include "src/network.hpp"
#include <wolfssh/error.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>
namespace term::ssh
{
struct Service::Client
{
    int fd;
    unsigned budget = 0;
    std::uint64_t started = 0, last_io = 0, now = 0, drain_started = 0, hold_until = 0;
    std::uint32_t address = 0;
    std::unique_ptr<Connection> connection;
    explicit Client(int socket, std::uint64_t time)
        : fd(socket), started(time), last_io(time), now(time)
    {
    }
    ~Client()
    {
        connection.reset();
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
    static int receive(void *opaque, unsigned char *bytes, unsigned size)
    {
        auto &self = *static_cast<Client *>(opaque);
        if (!self.budget)
            return WS_CBIO_ERR_WANT_READ;
        const auto count = ::recv(self.fd, bytes, std::min(size, self.budget), 0);
        if (count > 0)
        {
            self.budget -= count;
            self.last_io = self.now;
            return count;
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
        if (!self.budget)
            return WS_CBIO_ERR_WANT_WRITE;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto count = ::send(self.fd, bytes, std::min(size, self.budget), flags);
        if (count > 0)
        {
            self.budget -= count;
            self.last_io = self.now;
            return count;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return WS_CBIO_ERR_WANT_WRITE;
        return WS_CBIO_ERR_CONN_RST;
    }
};
Service::Service() noexcept = default;
Service::~Service()
{
    stop();
}
void Service::stop() noexcept
{
    if (listener_ >= 0)
    {
        ::close(listener_);
        listener_ = -1;
    }
    for (auto &client : clients_)
        client.reset();
    identity_.reset();
    if (initialized_)
        wolfSSH_Cleanup();
    initialized_ = false;
    port_ = 0;
    status_ = "SSH disabled";
}
bool Service::set_password(std::string_view value) noexcept
{
    if (!identity_ || !identity_->update_password(value))
        return false;
    // Connections hold views of the old identity. Drop authenticated and
    // pre-authentication clients before any more protocol processing.
    for (auto &client : clients_)
        client.reset();
    return true;
}
unsigned Service::clients() const noexcept
{
    unsigned count = 0;
    for (const auto &client : clients_)
        if (client)
            ++count;
    return count;
}
bool Service::start(const char *directory, const char *bind_address, unsigned port) noexcept
{
    stop();
    if (port > 65535 || !bind_address)
    {
        status_ = "Invalid SSH address or port";
        return false;
    }
    sockaddr_in address{};
#if defined(__FreeBSD__) || defined(__APPLE__)
    address.sin_len = sizeof(address);
#endif
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1)
    {
        status_ = "Invalid SSH bind address";
        return false;
    }
    if (wolfSSH_Init() != WS_SUCCESS)
    {
        status_ = "SSH crypto initialization failed";
        return false;
    }
    initialized_ = true;
    identity_ = std::make_unique<Identity>();
    if (!identity_->load_or_create(directory))
    {
        std::snprintf(error_.data(), error_.size(), "%s", identity_->error());
        stop();
        status_ = error_.data();
        return false;
    }
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
    {
        stop();
        status_ = "SSH socket creation failed";
        return false;
    }
    // Never identify an existing listener as ours and never share its address.
    int enabled = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    if (!nonblocking_socket(listener_) ||
        ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        ::listen(listener_, 4) != 0)
    {
        stop();
        status_ = "Cannot bind SSH port (busy or unavailable)";
        return false;
    }
    socklen_t length = sizeof(address);
    if (::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &length) != 0)
    {
        stop();
        status_ = "Cannot read SSH listener address";
        return false;
    }
    port_ = ntohs(address.sin_port);
    status_ = "PS5Term SSH server is listening";
    return true;
}
// Time from connect to a completed login. A person reads the host-key question and
// types the password; idle strangers lose their slot to new connections anyway.
static constexpr std::uint64_t login_grace_us = 60000000ULL;
void Service::tick(std::uint64_t now) noexcept
{
    if (listener_ < 0)
        return;
    // One accept per tick; excess connections are closed rather than retained.
    // After failed passwords, new connections from the same address wait before
    // their handshake starts, which limits guessing across connections. Each
    // connection allows only three tries.
    sockaddr_in peer{};
    socklen_t peer_length = sizeof(peer);
    const int fd = ::accept(listener_, reinterpret_cast<sockaddr *>(&peer), &peer_length);
    if (fd >= 0)
    {
        const std::uint32_t address = peer_length >= sizeof(peer) ? peer.sin_addr.s_addr : 0;
        auto slot =
            std::find_if(clients_.begin(), clients_.end(), [](const auto &c) { return !c; });
        if (slot == clients_.end())
        {
            // Silent connections must not lock out a real login: replace the oldest
            // that has not authenticated. Authenticated sessions are never displaced.
            auto oldest = clients_.end();
            for (auto it = clients_.begin(); it != clients_.end(); ++it)
                if (!(*it)->connection->authenticated() &&
                    (oldest == clients_.end() || (*it)->started < (*oldest)->started))
                    oldest = it;
            if (oldest != clients_.end())
            {
                oldest->reset();
                slot = oldest;
            }
        }
        if (slot == clients_.end() || !nonblocking_socket(fd))
            ::close(fd);
        else
        {
#ifdef SO_NOSIGPIPE
            int enabled = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
            int keepalive = 1;
            (void)::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
            auto client = std::make_unique<Client>(fd, now);
            client->address = address;
            for (const auto &penalty : penalties_)
                if (penalty.strikes && penalty.address == address && now < penalty.until)
                {
                    client->hold_until = penalty.until;
                    client->started = client->last_io = penalty.until;
                }
            const ServerConfig config{
                "ps5term",
                identity_->password(),
                "/data",
                identity_->key(),
                identity_->authorized_keys(),
                identity_.get(),
                [](void *context, std::string_view key, bool remove)
                { return static_cast<Identity *>(context)->update_authorized_key(key, remove); },
                guest_};
            client->connection = std::make_unique<Connection>(
                config, TransportIO{client.get(), Client::receive, Client::send});
            *slot = std::move(client);
        }
    }
    for (auto &client : clients_)
    {
        if (!client)
            continue;
        if (now < client->hold_until)
            continue; // Handshake starts when the backoff ends.
        client->budget = 256 * 1024;
        client->now = now;
        client->connection->set_time(now);
        client->connection->tick();
        if (!client->connection->draining())
            client->drain_started = 0;
        if (client->connection->draining() && !client->drain_started)
            client->drain_started = now;
        // A client ignoring our close must not hold its slot for the idle timeout.
        const bool expired =
            now < client->started || now < client->last_io ||
            (client->drain_started && now - client->drain_started > 10000000ULL) ||
            (now - client->last_io > 300000000ULL && !client->connection->long_lived()) ||
            (!client->connection->authenticated() && now - client->started > login_grace_us);
        if (client->connection->closed() || expired)
        {
            if (const auto failures = client->connection->password_failures();
                failures && !client->connection->authenticated())
            {
                // Strikes fade after ten quiet minutes; the wait grows to 30 seconds.
                Penalty *entry = nullptr;
                for (auto &penalty : penalties_)
                    if (penalty.strikes && penalty.address == client->address)
                        entry = &penalty;
                if (entry && now - entry->last > 600000000ULL)
                    entry->strikes = 0;
                if (!entry)
                {
                    // A free entry, otherwise the address that failed longest ago.
                    entry = &penalties_[0];
                    for (auto &penalty : penalties_)
                    {
                        if (!penalty.strikes)
                        {
                            entry = &penalty;
                            break;
                        }
                        if (penalty.last < entry->last)
                            entry = &penalty;
                    }
                    *entry = Penalty{client->address, 0, 0, 0};
                }
                ++entry->strikes;
                entry->last = now;
                entry->until =
                    now + std::min<std::uint64_t>(std::uint64_t(entry->strikes) * failures, 30) *
                              1000000ULL;
            }
            client.reset();
        }
    }
}
} // namespace term::ssh
