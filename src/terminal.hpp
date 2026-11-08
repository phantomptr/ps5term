// PS5Term - bounded terminal state and command editing. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "text_metrics.hpp"
namespace term
{
class Screen
{
  public:
    static constexpr std::size_t columns = text_metrics::columns, capacity = 512;
    void feed(std::string_view text) noexcept;
    void clear() noexcept;
    std::string_view line(std::size_t index) const noexcept;
    std::uint8_t color(std::size_t index, std::size_t col) const noexcept;
    std::size_t count() const noexcept
    {
        return count_;
    }
    // Lines that ever left the buffer (scrolled out or cleared): line(i) is
    // the (dropped() + i)-th line ever written, a stable id for selections.
    std::uint64_t dropped() const noexcept
    {
        return dropped_;
    }

  private:
    struct Line
    {
        std::array<char, columns> text{};
        std::array<std::uint8_t, columns> colors{};
        std::size_t size = 0;
    };
    std::array<Line, capacity> lines_{};
    std::size_t start_ = 0, count_ = 1, cursor_ = 0;
    std::uint64_t dropped_ = 0;
    unsigned escape_ = 0, csi_ = 0;
    std::uint8_t color_ = 7;
    void newline() noexcept;
};
class Editor
{
  public:
    static constexpr std::size_t limit = 512;
    void insert(std::string_view text) noexcept;
    void backspace() noexcept;
    void erase() noexcept;
    void left() noexcept
    {
        if (cursor_)
            --cursor_;
    }
    void right() noexcept
    {
        if (cursor_ < size_)
            ++cursor_;
    }
    void home() noexcept
    {
        cursor_ = 0;
    }
    void end() noexcept
    {
        cursor_ = size_;
    }
    void move_to(std::size_t position) noexcept
    {
        cursor_ = position < size_ ? position : size_;
    }
    void clear() noexcept
    {
        size_ = cursor_ = 0;
    }
    void commit() noexcept;
    void history(int direction) noexcept;
    std::string_view text() const noexcept
    {
        return {text_.data(), size_};
    }
    std::size_t cursor() const noexcept
    {
        return cursor_;
    }

  private:
    using Buffer = std::array<char, limit + 1>;
    Buffer text_{}, draft_{};
    std::array<Buffer, 32> history_{};
    std::array<std::size_t, 32> lengths_{};
    std::size_t size_ = 0, cursor_ = 0, history_count_ = 0, history_pos_ = 0, draft_size_ = 0;
};
} // namespace term
