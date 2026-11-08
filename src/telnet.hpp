// PS5Term - incremental Telnet framing; no terminal negotiation required by shsrv.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <string_view>
namespace term
{
class Telnet
{
  public:
    std::string_view feed(const char *bytes, std::size_t length) noexcept;
    std::string_view reply() noexcept
    {
        auto n = replies_;
        replies_ = 0;
        return {reply_.data(), n};
    }
    struct Packet
    {
        std::array<char, 8192> bytes{};
        std::size_t size = 0;
        std::string_view view() const noexcept
        {
            return {bytes.data(), size};
        }
    };
    static Packet encode(std::string_view input) noexcept;

  private:
    unsigned state_ = 0, command_ = 0;
    std::array<char, 4096> output_{}, reply_{};
    std::size_t replies_ = 0;
};
} // namespace term
