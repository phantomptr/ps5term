// PS5Term owned SSH identity. GPL-3.0-or-later.
#pragma once
#include <array>
#include <span>
#include <string_view>
namespace term::ssh
{
// Raw SSH wire-format public keys, not private keys. At most eight per service.
struct AuthorizedKey
{
    std::array<unsigned char,256> bytes{};
    std::size_t size=0;
};
class Identity
{
  public:
    ~Identity();
    Identity()=default;
    Identity(const Identity &)=delete;
    Identity &operator=(const Identity &)=delete;
    bool load_or_create(const char *directory) noexcept;
    std::string_view password() const noexcept { return {password_.data(),password_size_}; }
    std::span<const unsigned char> key() const noexcept { return {key_.data(),key_size_}; }
    std::span<const AuthorizedKey> authorized_keys() const noexcept { return {authorized_keys_.data(),authorized_count_}; }
    // Authenticated management only; update disk atomically, then live allowlist.
    bool update_password(std::string_view) noexcept;
    bool update_authorized_key(std::string_view,bool remove) noexcept;
    const char *error() const noexcept { return error_; }
  private:
    std::array<AuthorizedKey,8> authorized_keys_{};
    std::size_t authorized_count_=0;
    bool load_authorized_keys(const char *directory) noexcept;
    bool parse_authorized_keys(std::string_view) noexcept;
    std::array<char,1024> directory_{};
    std::array<char,128> password_{};
    std::array<unsigned char,4096> key_{};
    std::size_t password_size_=0,key_size_=0;
    std::array<char,128> detail_{};
    const char *error_="Identity unavailable";
    void clear() noexcept;
};
}
