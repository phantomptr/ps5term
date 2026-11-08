// PS5Term - shared native-pixel font and text geometry. GPL-3.0-or-later.
#pragma once
#include <cstddef>
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
constexpr std::size_t columns = 172, rows = 29, keyboard_rows = 19;
constexpr std::size_t input_columns = 168, input_margin = 4;
} // namespace term::text_metrics
