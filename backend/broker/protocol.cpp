// PS5Term - GPL-3.0-or-later.
#include "protocol.hpp"
#include <cstring>
namespace term::broker
{
namespace
{
void put(std::uint8_t *out, std::uint64_t n, unsigned size) noexcept
{ for (unsigned i = 0; i < size; ++i) out[size - i - 1] = static_cast<std::uint8_t>(n >> (i * 8)); }
std::uint64_t get(const std::uint8_t *in, unsigned size) noexcept
{ std::uint64_t n = 0; for (unsigned i = 0; i < size; ++i) n = (n << 8) | in[i]; return n; }
bool valid(const Credentials &value) noexcept
{ unsigned any = 0; for (auto ch : value.token) any |= ch; return value.generation && any; }
bool valid_type(Message type) noexcept
{ return type >= Message::hello && type <= Message::exit; }
bool valid_size(Message type, std::size_t size) noexcept
{
    switch (type)
    {
    case Message::data: return size <= max_payload_size;
    case Message::resize: case Message::signal: return size == 4;
    case Message::exit: return size == 8;
    default: return size == 0;
    }
}
void wipe(void *ptr, std::size_t size) noexcept
{ auto *out = static_cast<volatile std::uint8_t *>(ptr); while (size--) *out++ = 0; }
}
std::size_t encode(const Credentials &credentials, Message type, const std::uint8_t *data,
                   std::size_t size, std::uint8_t *out, std::size_t capacity) noexcept
{
    if (!valid(credentials) || !valid_type(type) || !valid_size(type, size) ||
        size > max_payload_size || !out || capacity < header_size + size || (!data && size)) return 0;
    std::memcpy(out, "P5TB", 4); put(out + 4, 1, 2); put(out + 6, static_cast<unsigned>(type), 2);
    put(out + 8, size, 4); put(out + 12, credentials.generation, 8);
    std::memcpy(out + 20, credentials.token.data(), credentials.token.size());
    if (size) std::memcpy(out + header_size, data, size);
    return header_size + size;
}
Decoder::Decoder(Credentials credentials) noexcept : credentials_(credentials)
{ if (!valid(credentials)) error_ = ProtocolError::authentication; }
Decoder::~Decoder() { wipe(&credentials_, sizeof(credentials_)); wipe(bytes_.data(), bytes_.size()); }
bool Decoder::check_header() noexcept
{
    if (std::memcmp(bytes_.data(), "P5TB", 4)) error_ = ProtocolError::magic;
    else if (get(bytes_.data() + 4, 2) != 1) error_ = ProtocolError::version;
    else
    {
        message_ = static_cast<Message>(get(bytes_.data() + 6, 2));
        length_ = static_cast<std::size_t>(get(bytes_.data() + 8, 4));
        unsigned difference = 0;
        for (std::size_t i = 0; i < credentials_.token.size(); ++i)
            difference |= bytes_[20+i] ^ credentials_.token[i];
        if (!valid_type(message_)) error_ = ProtocolError::type;
        else if (length_ > max_payload_size || !valid_size(message_, length_)) error_ = ProtocolError::length;
        else if (get(bytes_.data() + 12, 8) != credentials_.generation) error_ = ProtocolError::generation;
        else if (difference) error_ = ProtocolError::authentication;
    }
    header_ = error_ == ProtocolError::none;
    return header_;
}
std::size_t Decoder::feed(const std::uint8_t *data, std::size_t size) noexcept
{
    if (!data || ready_ || error_ != ProtocolError::none) return 0;
    std::size_t consumed = 0;
    while (consumed < size)
    {
        const auto needed = header_ ? header_size + length_ : header_size;
        const auto count = needed - used_ < size - consumed ? needed - used_ : size - consumed;
        if (count) std::memcpy(bytes_.data() + used_, data + consumed, count);
        used_ += count; consumed += count;
        if (!header_ && used_ == header_size && !check_header()) break;
        if (header_ && used_ == header_size + length_) { ready_ = true; break; }
    }
    return consumed;
}
void Decoder::consume() noexcept
{
    if (!ready_) return;
    wipe(bytes_.data(), used_);
    used_ = length_ = 0; ready_ = header_ = false;
}
void Lease::start(std::uint64_t generation, std::uint64_t now) noexcept
{ generation_ = generation; last_ = now; }
bool Lease::alive(std::uint64_t now) const noexcept
{ return generation_ && now >= last_ && now - last_ < lease_us; }
bool Lease::renew(std::uint64_t generation, std::uint64_t now) noexcept
{
    if (generation != generation_ || !alive(now)) return false;
    last_ = now; return true;
}
} // namespace term::broker
