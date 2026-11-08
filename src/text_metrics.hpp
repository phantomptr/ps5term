// PS5Term - shared native-pixel font and text geometry. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string_view>
namespace term::text_metrics
{
constexpr unsigned advance(unsigned style) noexcept
{
    return style == 1 ? 7 : style == 3 ? 15 : 10; // anything else is normal size
}
constexpr unsigned height(unsigned style) noexcept
{
    return style == 1 ? 16 : style == 3 ? 30 : 20; // never zero: callers divide by it
}
constexpr unsigned cell_width = advance(2), row_height = height(2);
// Characters (cells) in UTF-8 text: every byte that does not continue a character.
constexpr std::size_t length(std::string_view text) noexcept
{
    std::size_t count = 0;
    for (const char byte : text)
        count += (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U;
    return count;
}
constexpr std::size_t columns = 172, rows = 29, keyboard_rows = 19;
constexpr std::size_t input_columns = 168, input_margin = 4;
} // namespace term::text_metrics
