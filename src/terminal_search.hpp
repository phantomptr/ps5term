// PS5Term GPL-3.0-or-later. Bounded incremental literal UTF-8 search.
#pragma once
#include "terminal_model.hpp"
#include <algorithm>
namespace term
{
// A small query editor with codepoint boundaries. Reject control characters and
// malformed UTF-8 atomically, so clipboard text cannot become terminal input.
class SearchQuery
{
  public:
    static constexpr std::size_t limit = 96;
    std::string_view text() const noexcept
    {
        return {data_.data(), size_};
    }
    std::size_t cursor() const noexcept
    {
        return cursor_;
    }
    void clear() noexcept
    {
        size_ = cursor_ = 0;
    }
    void home() noexcept
    {
        cursor_ = 0;
    }
    void end() noexcept
    {
        cursor_ = size_;
    }
    void left() noexcept
    {
        if (!cursor_)
            return;
        do
        {
            --cursor_;
        } while (cursor_ && continuation(data_[cursor_]));
    }
    void right() noexcept
    {
        if (cursor_ == size_)
            return;
        do
        {
            ++cursor_;
        } while (cursor_ < size_ && continuation(data_[cursor_]));
    }
    void erase() noexcept
    {
        const auto first = cursor_;
        right();
        const auto removed = cursor_ - first;
        std::move(data_.begin() + cursor_, data_.begin() + size_, data_.begin() + first);
        size_ -= removed;
        cursor_ = first;
    }
    void backspace() noexcept
    {
        if (cursor_)
        {
            left();
            erase();
        }
    }
    bool insert(std::string_view value) noexcept
    {
        if (value.size() > limit - size_)
            return false;
        for (std::size_t i = 0; i < value.size();)
        {
            const auto c = static_cast<unsigned char>(value[i++]);
            if (c < 32 || c == 127)
                return false;
            if (c < 128)
                continue;
            const unsigned n = c >= 0xc2 && c <= 0xdf   ? 1
                               : c >= 0xe0 && c <= 0xef ? 2
                               : c >= 0xf0 && c <= 0xf4 ? 3
                                                        : 0;
            if (!n || n > value.size() - i)
                return false;
            unsigned cp = c & ((1u << (6 - n)) - 1);
            for (unsigned j = 0; j < n; ++j)
            {
                if (!continuation(value[i]))
                    return false;
                cp = (cp << 6) | (static_cast<unsigned char>(value[i++]) & 63);
            }
            if (cp < (n == 1   ? 0x80u
                      : n == 2 ? 0x800u
                               : 0x10000u) ||
                cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp < 0xa0))
                return false;
        }
        std::move_backward(data_.begin() + cursor_, data_.begin() + size_,
                           data_.begin() + size_ + value.size());
        std::copy(value.begin(), value.end(), data_.begin() + cursor_);
        cursor_ += value.size();
        size_ += value.size();
        return true;
    }

  private:
    std::array<char, limit> data_{};
    std::size_t size_ = 0, cursor_ = 0;
    static bool continuation(char c) noexcept
    {
        return (static_cast<unsigned char>(c) & 0xc0) == 0x80;
    }
};
class TerminalSearch
{
  public:
    static constexpr std::size_t capacity = 2048, query_limit = 96;
    struct Match
    {
        std::uint64_t line = 0;
        std::size_t first = 0, last = 0;
    };
    bool start(const TerminalModel &model, std::string_view query) noexcept
    {
        count_ = matched_ = column_ = bytes_ = 0;
        truncated_ = busy_ = false;
        size_ = query.size();
        if (!size_ || size_ > query_limit)
            return false;
        std::copy(query.begin(), query.end(), query_.begin());
        prefix_[0] = 0;
        for (std::size_t i = 1, j = 0; i < size_; ++i)
        {
            while (j && query_[i] != query_[j])
                j = prefix_[j - 1];
            if (query_[i] == query_[j])
                ++j;
            prefix_[i] = j;
        }
        line_ = model.alternate_screen() ? model.history_end() : model.history_first();
        end_ = model.history_end() + static_cast<std::uint64_t>(model.rows());
        busy_ = true;
        return true;
    }
    void step(const TerminalModel &model, std::size_t budget = 4096) noexcept
    {
        if (!busy_)
            return;
        if (line_ < model.history_first())
        {
            line_ = model.history_first();
            column_ = matched_ = 0;
        }
        while (budget && line_ < end_)
        {
            const auto history = model.history_end();
            const auto cols = line_ < history ? std::min(model.history_columns(line_),
                                                         static_cast<std::size_t>(model.columns()))
                                              : static_cast<std::size_t>(model.columns());
            if (column_ >= cols)
            {
                ++line_;
                column_ = matched_ = 0;
                continue;
            }
            --budget;
            auto cell = line_ < history ? model.history_cell(line_, column_)
                                        : model.cell(static_cast<int>(line_ - history),
                                                     static_cast<int>(column_));
            const auto col = column_++;
            if (!cell.width && !cell.codepoints[0])
                continue; // wide-character continuation
            if (!cell.codepoints[0])
                emit(' ', col, col);
            else
                for (auto cp : cell.codepoints)
                {
                    if (!cp)
                        break;
                    const auto last = col + (cell.width == 2 ? 1 : 0);
                    if (cp <= 0x7f)
                        emit(static_cast<char>(cp), col, last);
                    else
                    {
                        if (cp > 0xffff)
                            emit(static_cast<char>(0xf0 | (cp >> 18)), col, last);
                        if (cp > 0x7ff)
                            emit(static_cast<char>((cp > 0xffff ? 0x80 : 0xe0) | ((cp >> 12) & 63)),
                                 col, last);
                        emit(static_cast<char>((cp > 0x7ff ? 0x80 : 0xc0) | ((cp >> 6) & 63)), col,
                             last);
                        emit(static_cast<char>(0x80 | (cp & 63)), col, last);
                    }
                }
        }
        if (line_ >= end_)
            busy_ = false;
    }
    bool busy() const noexcept
    {
        return busy_;
    }
    bool truncated() const noexcept
    {
        return truncated_;
    }
    std::size_t count() const noexcept
    {
        return count_;
    }
    Match match(std::size_t i) const noexcept
    {
        return i < count_ ? matches_[i] : Match{};
    }

  private:
    std::array<Match, capacity> matches_{};
    std::array<char, query_limit> query_{};
    std::array<std::size_t, query_limit> prefix_{}, starts_{};
    std::size_t size_ = 0, count_ = 0, matched_ = 0, column_ = 0, bytes_ = 0;
    std::uint64_t line_ = 0, end_ = 0;
    bool busy_ = false, truncated_ = false;
    void emit(char byte, std::size_t col, std::size_t last) noexcept
    {
        starts_[bytes_ % query_limit] = col;
        ++bytes_;
        while (matched_ && byte != query_[matched_])
            matched_ = prefix_[matched_ - 1];
        if (byte == query_[matched_])
            ++matched_;
        if (matched_ == size_)
        {
            if (count_ < capacity)
                matches_[count_++] = {line_, starts_[(bytes_ - size_) % query_limit], last};
            else
                truncated_ = true;
            matched_ = prefix_[matched_ - 1];
        }
    }
};
} // namespace term
