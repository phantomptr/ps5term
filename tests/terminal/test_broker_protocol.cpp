// PS5Term - broker framing and owner lease regressions. GPL-3.0-or-later.
#include "broker/protocol.hpp"
#include <array>
#include <cassert>
#include <cstring>
using namespace term::broker;
int main()
{
    Credentials credentials{};
    credentials.generation = 42;
    credentials.token[0] = 1;
    std::array<std::uint8_t, max_frame_size> bytes{};
    const std::uint8_t text[] = {'h', 'i'};
    auto n = encode(credentials, Message::data, text, sizeof(text), bytes.data(), bytes.size());
    assert(n == header_size + 2);
    Decoder decoder(credentials);
    for (std::size_t i = 0; i < n; ++i)
    {
        assert(decoder.feed(bytes.data() + i, 1) == 1);
        assert(decoder.ready() == (i + 1 == n));
    }
    assert(decoder.message() == Message::data && decoder.size() == 2);
    assert(std::memcmp(decoder.data(), text, 2) == 0);
    assert(decoder.feed(bytes.data(), n) == 0); // explicit consume prevents overwrite
    decoder.consume();
    assert(decoder.feed(bytes.data(), n) == n && decoder.ready());
    auto changed = credentials;
    changed.token[0] ^= 1;
    Decoder wrong_token(changed);
    wrong_token.feed(bytes.data(), n);
    assert(wrong_token.error() == ProtocolError::authentication);
    changed = credentials;
    ++changed.generation;
    Decoder stale(changed);
    stale.feed(bytes.data(), n);
    assert(stale.error() == ProtocolError::generation);
    auto save = bytes;
    bytes[5] = 2;
    Decoder wrong_version(credentials);
    wrong_version.feed(bytes.data(), n);
    assert(wrong_version.error() == ProtocolError::version);
    bytes = save;
    bytes[8] = 0;
    bytes[9] = 1;
    bytes[10] = 0;
    bytes[11] = 1; // 65537
    Decoder oversize(credentials);
    oversize.feed(bytes.data(), header_size);
    assert(oversize.error() == ProtocolError::length);
    assert(!encode(credentials, Message::resize, text, 2, bytes.data(), bytes.size()));
    Decoder missing(Credentials{});
    assert(missing.error() == ProtocolError::authentication);
    Lease lease;
    lease.start(42, 100);
    assert(lease.alive(10000100 - 1));
    assert(!lease.renew(43, 1000));
    assert(!lease.alive(10000100));
    assert(!lease.renew(42, 10000101)); // expired ownership cannot be resurrected
    lease.start(43, 200);
    assert(lease.renew(43, 5000000));
    assert(lease.alive(14999999));
    assert(!lease.alive(15000000));
    lease.start(44, 300);
    assert(!lease.alive(299)); // clock discontinuity fails closed
    lease.disconnect();
    assert(!lease.alive(300));
}
