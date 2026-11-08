// PS5Term - GPL-3.0-or-later.
#include "scrollback.hpp"
#include <array>
#include <cassert>
using term::Scrollback;
using Cell = term::TerminalModel::Cell;
int main()
{
    Scrollback history(3, 4096);
    assert(history.healthy() && history.size() == 0 && history.first() == 1 && history.end() == 1);
    std::array<Cell, 2> line{};
    line[0].codepoints[0] = 0x754c;
    line[0].width = 2;
    line[0].foreground = {12, 34, 56};
    line[0].bold = true;
    assert(history.push(line));
    assert(history.first() == 1 && history.end() == 2 && history.columns(1) == 2);
    assert(history.cell(1, 0) == line[0] && history.cell(1, 1) == line[1]);
    assert(history.cell(1, 2).width == 0 && history.columns(0) == 0);
    assert(history.push(line) && history.push(line) && history.push(line));
    assert(history.size() == 3 && history.first() == 2 && history.end() == 5);
    assert(history.columns(1) == 0 && history.cell(1, 0).width == 0);
    const auto used = history.bytes();
    history.clear();
    assert(history.size() == 0 && history.first() == 5 && history.end() == 5 &&
           history.bytes() < used);
    assert(history.push(std::span<const Cell>(line.data(), 1)) && history.columns(5) == 1);
    // Metadata and every stored cell count towards the byte bound.
    Scrollback bounded(3, 256);
    assert(bounded.healthy());
    for (int i = 0; i < 100; ++i)
    {
        assert(bounded.push(line));
        assert(bounded.bytes() <= 256 && bounded.size() <= 3);
    }
    assert(bounded.first() > 1 && bounded.end() == 101);
    const auto next = bounded.end();
    std::array<Cell, 240> huge{};
    for (auto &cell : huge)
        cell.codepoints[0] = 'x'; // Blanks are trimmed; real text is not.
    assert(!bounded.push(huge) && bounded.end() == next && bounded.healthy());
    // A blank line costs one cell, whatever the terminal's width, and keeps its width.
    Scrollback trimmed(10, 4096);
    std::array<Cell, 240> empty{};
    assert(trimmed.push(empty) && trimmed.columns(1) == 240);
    assert(trimmed.bytes() <= 10 * sizeof(void *) * 3 + sizeof(Cell));
    assert(!bounded.push({}) && bounded.end() == next);
    Scrollback invalid(10001, 4096), too_small(3, 1);
    assert(!invalid.healthy() && !too_small.healthy());
    assert(!invalid.push(line) && !too_small.push(line));
}
