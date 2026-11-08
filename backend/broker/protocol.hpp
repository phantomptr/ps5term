// PS5Term - owned broker protocol v1. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace term::broker
{
inline constexpr std::size_t header_size = 52, max_payload_size = 65536;
inline constexpr std::size_t max_frame_size = header_size + max_payload_size;
inline constexpr std::uint64_t lease_us = 10000000;
enum class Message : std::uint16_t { hello = 1, data, resize, signal, close, heartbeat, exit };
enum class ProtocolError : std::uint8_t { none, magic, version, type, length, authentication, generation };
struct Credentials
{
    std::uint64_t generation = 0;
    std::array<std::uint8_t, 32> token{};
};
// Credentials come from OS entropy, never from clock/pid. No default token is accepted.
std::size_t encode(const Credentials &, Message, const std::uint8_t *, std::size_t,
                   std::uint8_t *, std::size_t) noexcept;
class Decoder
{
  public:
    explicit Decoder(Credentials) noexcept;
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    std::size_t feed(const std::uint8_t *, std::size_t) noexcept;
    bool ready() const noexcept { return ready_; }
    ProtocolError error() const noexcept { return error_; }
    Message message() const noexcept { return message_; }
    const std::uint8_t *data() const noexcept { return ready_ ? bytes_.data() + header_size : nullptr; }
    std::size_t size() const noexcept { return ready_ ? length_ : 0; }
    void consume() noexcept;
  private:
    Credentials credentials_;
    std::array<std::uint8_t, max_frame_size> bytes_{};
    std::size_t used_ = 0, length_ = 0;
    ProtocolError error_ = ProtocolError::none;
    Message message_ = Message::hello;
    bool ready_ = false, header_ = false;
    bool check_header() noexcept;
};
class Lease
{
  public:
    void start(std::uint64_t generation, std::uint64_t now) noexcept;
    bool renew(std::uint64_t generation, std::uint64_t now) noexcept;
    bool alive(std::uint64_t now) const noexcept;
    void disconnect() noexcept { generation_ = 0; }
  private:
    std::uint64_t generation_ = 0, last_ = 0;
};
} // namespace term::broker
