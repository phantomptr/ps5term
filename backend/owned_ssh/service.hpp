// PS5Term owned SSH service. GPL-3.0-or-later.
#pragma once
#include "connection.hpp"
#include "identity.hpp"
#include <array>
#include <cstdint>
#include <memory>
namespace term::ssh
{
// Exactly one owner calls every method, including destruction. No background
// threads, process-global cwd/fds, signals, injected payloads or loader calls.
class Service
{
  public:
    Service() noexcept;
    ~Service();
    Service(const Service &)=delete;
    Service &operator=(const Service &)=delete;
    bool start(const char *directory,const char *bind_address,unsigned port) noexcept;
    void tick(std::uint64_t monotonic_us) noexcept;
    void stop() noexcept;
    bool set_password(std::string_view) noexcept;
    // Borrowed; must outlive the service. Enables the "linux" login.
    void set_guest(GuestAccess *guest) noexcept { guest_=guest; }
    bool listening() const noexcept { return listener_>=0; }
    unsigned port() const noexcept { return port_; }
    unsigned clients() const noexcept;
    std::string_view password() const noexcept { return identity_ ? identity_->password() : std::string_view{}; }
    const char *status() const noexcept { return status_; }
  private:
    struct Client;
    int listener_=-1;
    unsigned port_=0;
    bool initialized_=false;
    // Backoff after failed passwords, per source address, so one guessing host
    // cannot hold up logins from anywhere else.
    struct Penalty { std::uint32_t address=0; unsigned strikes=0; std::uint64_t until=0,last=0; };
    std::array<Penalty,8> penalties_{};
    GuestAccess *guest_=nullptr;
    std::array<char,128> error_{};
    const char *status_="SSH disabled";
    std::unique_ptr<Identity> identity_;
    std::array<std::unique_ptr<Client>,4> clients_{};
};
}
