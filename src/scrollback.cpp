// PS5Term - GPL-3.0-or-later.
#include "scrollback.hpp"
#include <algorithm>
#include <new>
namespace term
{
Scrollback::Scrollback(std::size_t lines, std::size_t bytes) noexcept
{
    if (!lines || lines > max_lines || bytes > max_bytes ||
        bytes < lines * sizeof(Line) + sizeof(Cell))
        return;
    lines_ = new (std::nothrow) Line[lines]{};
    if (!lines_)
        return;
    capacity_ = lines;
    limit_ = bytes;
    used_ = lines * sizeof(Line);
}
Scrollback::~Scrollback()
{
    clear();
    delete[] lines_;
}
void Scrollback::evict() noexcept
{
    if (!count_)
        return;
    auto &line = lines_[head_];
    used_ -= line.stored * sizeof(Cell);
    delete[] line.cells;
    line = {};
    head_ = (head_ + 1) % capacity_;
    --count_;
}
void Scrollback::clear() noexcept
{
    while (count_)
        evict();
    head_ = 0;
}
bool Scrollback::push(std::span<const Cell> cells) noexcept
{
    if (!healthy() || cells.empty() || cells.size() > TerminalModel::max_columns ||
        next_ == UINT64_MAX)
        return false;
    std::size_t stored = cells.size();
    const auto blank = [](const Cell &cell) noexcept
    {
        return (!cell.codepoints[0] || cell.codepoints[0] == ' ') && cell.default_background &&
               !cell.reverse && !cell.underline && !cell.strike;
    };
    while (stored > 1 && blank(cells[stored - 1]))
        --stored;
    const auto cost = stored * sizeof(Cell);
    if (cost > limit_ - capacity_ * sizeof(Line))
        return false;
    while (count_ == capacity_ || cost > limit_ - used_)
        evict();
    auto *copy = new (std::nothrow) Cell[stored];
    if (!copy)
    {
        failed_ = true;
        return false;
    }
    std::copy_n(cells.begin(), stored, copy);
    lines_[(head_ + count_) % capacity_] = {copy, cells.size(), stored};
    ++count_;
    ++next_;
    used_ += cost;
    return true;
}
const Scrollback::Line *Scrollback::find(std::uint64_t id) const noexcept
{
    if (id < first() || id >= next_)
        return nullptr;
    return &lines_[(head_ + static_cast<std::size_t>(id - first())) % capacity_];
}
std::size_t Scrollback::columns(std::uint64_t id) const noexcept
{
    auto *line = find(id);
    return line ? line->columns : 0;
}
Scrollback::Cell Scrollback::cell(std::uint64_t id, std::size_t column) const noexcept
{
    auto *line = find(id);
    return line && column < line->stored ? line->cells[column] : Cell{};
}
} // namespace term
