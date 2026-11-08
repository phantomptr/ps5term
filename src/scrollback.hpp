// PS5Term - bounded immutable terminal line snapshots. GPL-3.0-or-later.
#pragma once
#include "terminal_model.hpp"
namespace term
{
class Scrollback
{
  public:
    using Cell = TerminalModel::Cell;
    static constexpr std::size_t max_lines = 10000, max_bytes = 8 * 1024 * 1024;
    Scrollback(std::size_t lines = max_lines, std::size_t bytes = max_bytes) noexcept;
    ~Scrollback();
    Scrollback(const Scrollback &) = delete;
    Scrollback &operator=(const Scrollback &) = delete;
    bool healthy() const noexcept
    {
        return lines_ && !failed_;
    }
    // Reject empty/oversized lines without changing IDs. Allocation failure
    // latches unhealthy; already retained snapshots remain readable.
    bool push(std::span<const Cell> cells) noexcept;
    void clear() noexcept;
    std::size_t size() const noexcept
    {
        return count_;
    }
    std::size_t bytes() const noexcept
    {
        return used_;
    }
    std::uint64_t first() const noexcept
    {
        return next_ - count_;
    }
    std::uint64_t end() const noexcept
    {
        return next_;
    }
    std::size_t columns(std::uint64_t id) const noexcept;
    Cell cell(std::uint64_t id, std::size_t column) const noexcept;

  private:
    // Trailing blanks are not stored; columns keeps the line's real width.
    struct Line
    {
        Cell *cells = nullptr;
        std::size_t columns = 0, stored = 0;
    };
    Line *lines_ = nullptr;
    std::size_t capacity_ = 0, limit_ = 0, used_ = 0, head_ = 0, count_ = 0;
    std::uint64_t next_ = 1;
    bool failed_ = false;
    void evict() noexcept;
    const Line *find(std::uint64_t id) const noexcept;
};
} // namespace term
