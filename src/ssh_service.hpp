// PS5Term - bundled SSH service lifecycle. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <memory>
#if defined(PS5TERM_OWNED_SSH)
namespace term::ssh
{
class Service;
class GuestAccess;
} // namespace term::ssh
#endif
namespace term
{
#if defined(PS5TERM_OWNED_SSH)
class SshService
{
  public:
    explicit SshService(const char *directory = "/data/ps5term/ssh", unsigned primary = 2222,
                        unsigned fallback = 2223, unsigned unused_loader = 0) noexcept;
    ~SshService();
    SshService(const SshService &) = delete;
    SshService &operator=(const SshService &) = delete;
    void tick(std::uint64_t now) noexcept;
    void retry() noexcept;
    void cancel() noexcept;
    bool uploading() const noexcept
    {
        return false;
    }
    bool uploaded() const noexcept
    {
        return false;
    }
    bool set_password(std::string_view value) noexcept;
    void set_guest(ssh::GuestAccess *guest) noexcept; // Enables the "linux" login.
    bool listening() const noexcept;
    bool failed() const noexcept
    {
        return failed_;
    }
    unsigned port() const noexcept;
    std::string_view status() const noexcept;

  private:
    const char *directory_;
    unsigned port_;
    bool attempted_ = false, failed_ = false;
    std::unique_ptr<ssh::Service> service_;
};
#else
class SshService
{
  public:
    explicit SshService(const char *payload = "/app0/assets/backend/ps5sshd.elf",
                        unsigned primary = 22, unsigned fallback = 2222,
                        unsigned loader = 9021) noexcept;
    ~SshService();
    SshService(const SshService &) = delete;
    SshService &operator=(const SshService &) = delete;
    void tick(std::uint64_t now) noexcept;
    void retry() noexcept;
    void cancel() noexcept;
    bool uploading() const noexcept
    {
        return state_ == State::uploading;
    }
    bool listening() const noexcept
    {
        return state_ == State::ready;
    }
    bool failed() const noexcept
    {
        return state_ == State::failed;
    }
    bool uploaded() const noexcept
    {
        return uploaded_;
    }
    unsigned port() const noexcept
    {
        return ports_[which_];
    }
    std::string_view status() const noexcept
    {
        return status_;
    }

  private:
    enum class State
    {
        idle,
        connecting,
        banner,
        loader,
        uploading,
        waiting,
        ready,
        failed
    };
    State state_ = State::idle;
    const char *payload_;
    unsigned ports_[2], loader_port_, which_ = 0;
    int socket_ = -1, file_ = -1;
    std::uint64_t deadline_ = 0, startup_deadline_ = 0, next_ = 0;
    bool attempted_ = false, uploaded_ = false, stop_after_upload_ = false;
    std::array<char, 8192> buffer_{};
    std::size_t size_ = 0, sent_ = 0;
    std::string_view status_ = "Starting bundled SSH service";
    bool connect(unsigned port) noexcept;
    void close_socket() noexcept;
    void fail(std::string_view message) noexcept;
    void next_probe(std::uint64_t now) noexcept;
};
#endif
} // namespace term
