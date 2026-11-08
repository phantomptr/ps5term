// PS5Term - GPL-3.0-or-later.
#include "telnet.hpp"
namespace term
{
std::string_view Telnet::feed(const char *bytes, std::size_t length) noexcept
{
    std::size_t n = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        auto ch = static_cast<unsigned char>(bytes[i]);
        if (state_ == 0)
        {
            if (ch == 255)
                state_ = 1;
            else if (n < output_.size())
                output_[n++] = static_cast<char>(ch);
        }
        else if (state_ == 1)
        {
            if (ch == 255)
            {
                if (n < output_.size())
                    output_[n++] = char(255);
                state_ = 0;
            }
            else if (ch >= 251 && ch <= 254)
            {
                command_ = ch;
                state_ = 2;
            }
            else if (ch == 250)
                state_ = 3;
            else
                state_ = 0;
        }
        else if (state_ == 2)
        {
            if ((command_ == 251 || command_ == 253) && replies_ + 3 <= reply_.size())
            {
                reply_[replies_++] = char(255);
                reply_[replies_++] = command_ == 251 ? char(254) : char(252);
                reply_[replies_++] = static_cast<char>(ch);
            }
            state_ = 0;
        }
        else if (state_ == 3)
        {
            if (ch == 255)
                state_ = 4;
        }
        else if (state_ == 4)
            state_ = ch == 240 ? 0 : 3;
    }
    return {output_.data(), n};
}
Telnet::Packet Telnet::encode(std::string_view input) noexcept
{
    Packet p;
    for (unsigned char ch : input)
    {
        if (p.size + 2 > p.bytes.size())
            break;
        p.bytes[p.size++] = static_cast<char>(ch);
        if (ch == 255)
            p.bytes[p.size++] = char(255);
    }
    return p;
}
} // namespace term
