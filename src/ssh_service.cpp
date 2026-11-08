#if !defined(PS5TERM_OWNED_SSH)
// PS5Term - nonblocking SSH startup through the console's local loader.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ssh_service.hpp"
#include "i18n.hpp"
#include "network.hpp"
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
namespace term
{
SshService::SshService(const char *payload, unsigned primary, unsigned fallback,
                       unsigned loader) noexcept
    : payload_(payload), ports_{primary, fallback}, loader_port_(loader)
{
}
SshService::~SshService()
{
    close_socket();
    if (file_ >= 0)
        ::close(file_);
}
void SshService::close_socket() noexcept
{
    if (socket_ >= 0)
        ::close(socket_);
    socket_ = -1;
    size_ = sent_ = 0;
}
void SshService::fail(std::string_view message) noexcept
{
    close_socket();
    if (file_ >= 0)
        ::close(file_);
    file_ = -1;
    state_ = State::failed;
    status_ = message;
}
void SshService::retry() noexcept
{
    if (uploading())
    {
        stop_after_upload_ = false;
        return;
    }
    close_socket();
    if (file_ >= 0)
        ::close(file_);
    file_ = -1;
    state_ = State::idle;
    attempted_ = uploaded_ = stop_after_upload_ = false;
    which_ = 0;
}
void SshService::cancel() noexcept
{
    // Finish an in-flight ELF stream; the disabled preference prevents serving.
    if (uploading())
        stop_after_upload_ = true;
    else
        retry();
}
bool SshService::connect(unsigned port) noexcept
{
    close_socket();
    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0)
        return false;
    if (!nonblocking_socket(socket_))
    {
        close_socket();
        return false;
    }
#ifdef SO_NOSIGPIPE
    int yes = 1;
    ::setsockopt(socket_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    auto address = loopback_address(port);
    int result = ::connect(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (result < 0 && errno != EINPROGRESS)
    {
        close_socket();
        return false;
    }
    return true;
}
void SshService::next_probe(std::uint64_t now) noexcept
{
    close_socket();
    if (which_ == 0)
    {
        which_ = 1;
        state_ = State::idle;
        return;
    }
    if (!attempted_)
    {
        attempted_ = true;
        status_ = N_("Starting SSH server via local ELF loader");
        if (!connect(loader_port_))
        {
            fail(N_("Start elfldr on port 9021, then press Square to retry"));
            return;
        }
        state_ = State::loader;
        deadline_ = now + 3000000;
        return;
    }
    if (startup_deadline_ == 0 || now >= startup_deadline_)
    {
        fail(N_("Bundled SSH server did not become ready; check console notification"));
        return;
    }
    which_ = 0;
    state_ = State::waiting;
    next_ = now + 500000;
}
void SshService::tick(std::uint64_t now) noexcept
{
    if (state_ == State::failed)
        return;
    if (state_ == State::ready)
    {
        if (now < next_)
            return;
        which_ = 0;
        state_ = State::idle;
        // Monitoring never silently uploads a second payload.
        attempted_ = true;
        startup_deadline_ = 0;
    }
    if (state_ == State::waiting)
    {
        if (now < next_)
            return;
        state_ = State::idle;
    }
    if (state_ == State::idle)
    {
        deadline_ = now + 1500000;
        if (!connect(ports_[which_]))
        {
            next_probe(now);
            return;
        }
        state_ = State::connecting;
    }
    if (state_ == State::connecting || state_ == State::loader)
    {
        pollfd descriptor{socket_, POLLOUT, 0};
        int result = ::poll(&descriptor, 1, 0);
        if (result <= 0)
        {
            if (now >= deadline_)
            {
                if (state_ == State::loader)
                    fail(N_("ELF loader connection timed out"));
                else
                    next_probe(now);
            }
            return;
        }
        int error = 0;
        socklen_t length = sizeof(error);
        if (::getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error)
        {
            if (state_ == State::loader)
                fail(N_("ELF loader unavailable on port 9021; press Square to retry"));
            else
                next_probe(now);
            return;
        }
        if (state_ == State::loader)
        {
            file_ = ::open(payload_, O_RDONLY);
            if (file_ < 0)
            {
                fail(N_("Bundled SSH server asset missing"));
                return;
            }
            state_ = State::uploading;
            deadline_ = now + 60000000;
        }
        else
        {
            state_ = State::banner;
            size_ = 0;
        }
    }
    if (state_ == State::banner)
    {
        auto count = ::recv(socket_, buffer_.data() + size_, buffer_.size() - size_, 0);
        if (count > 0)
        {
            size_ += static_cast<std::size_t>(count);
            std::string_view banner(buffer_.data(), size_);
            if (banner.find('\n') != std::string_view::npos)
            {
                if (banner.starts_with("SSH-2.0-PS5Term_1.0\r\n"))
                {
                    close_socket();
                    state_ = State::ready;
                    status_ = N_("Bundled SSH server is listening");
                    next_ = now + 10000000;
                }
                else
                    next_probe(now);
                return;
            }
            if (size_ == buffer_.size())
            {
                next_probe(now);
                return;
            }
        }
        else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        {
            next_probe(now);
            return;
        }
        if (now >= deadline_)
            next_probe(now);
        return;
    }
    if (state_ != State::uploading)
        return;
    if (now >= deadline_)
    {
        fail(N_("SSH server upload timed out"));
        return;
    }
    if (sent_ == size_)
    {
        auto count = ::read(file_, buffer_.data(), buffer_.size());
        if (count < 0)
        {
            fail(N_("Cannot read bundled SSH server"));
            return;
        }
        if (count == 0)
        {
            ::close(file_);
            file_ = -1;
            ::shutdown(socket_, SHUT_WR);
            close_socket();
            uploaded_ = true;
            if (stop_after_upload_)
            {
                state_ = State::failed;
                status_ = N_("SSH disabled");
                return;
            }
            which_ = 0;
            state_ = State::waiting;
            next_ = now + 500000;
            startup_deadline_ = now + 60000000;
            status_ = N_("Waiting for SSH host keys and listener");
            return;
        }
        size_ = static_cast<std::size_t>(count);
        sent_ = 0;
    }
    auto count = ::send(socket_, buffer_.data() + sent_, size_ - sent_,
#ifdef MSG_NOSIGNAL
                        MSG_NOSIGNAL
#else
                        0
#endif
    );
    if (count > 0)
        sent_ += static_cast<std::size_t>(count);
    else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        fail(N_("SSH server upload failed"));
}
} // namespace term

#endif
