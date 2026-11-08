// PS5Term - GPL-3.0-or-later.
#include "terminal.hpp"
namespace term
{
void Screen::clear() noexcept
{
    dropped_ += count_;
    for (auto &l : lines_)
        l = Line{};
    start_ = cursor_ = 0;
    count_ = 1;
    escape_ = csi_ = 0;
    color_ = 7;
}
std::string_view Screen::line(std::size_t n) const noexcept
{
    if (n >= count_)
        return {};
    const auto &l = lines_[(start_ + n) % capacity];
    return {l.text.data(), l.size};
}
std::uint8_t Screen::color(std::size_t n, std::size_t col) const noexcept
{
    return n < count_ && col < columns ? lines_[(start_ + n) % capacity].colors[col] : 7;
}
void Screen::newline() noexcept
{
    if (count_ == capacity)
    {
        start_ = (start_ + 1) % capacity;
        ++dropped_;
    }
    else
        ++count_;
    lines_[(start_ + count_ - 1) % capacity] = Line{};
    cursor_ = 0;
}
void Screen::feed(std::string_view text) noexcept
{
    for (unsigned char ch : text)
    {
        auto &l = lines_[(start_ + count_ - 1) % capacity];
        if (escape_ == 1)
        {
            escape_ = ch == '[' ? 2 : 0;
            csi_ = 0;
            continue;
        }
        if (escape_ == 2)
        {
            if (ch >= '0' && ch <= '9')
            {
                csi_ = csi_ < 10000 ? csi_ * 10 + ch - '0' : 10000;
                continue;
            }
            if (ch == 'm' || ch == ';')
            {
                if (csi_ == 0)
                    color_ = 7;
                else if (csi_ >= 30 && csi_ <= 37)
                    color_ = csi_ - 30;
            }
            if (ch == ';')
            {
                csi_ = 0;
                continue;
            }
            if (ch == 'K')
            {
                if (csi_ == 2)
                {
                    l = Line{};
                    cursor_ = 0;
                }
                else
                {
                    l.size = cursor_;
                }
            }
            if (ch == 'J' && csi_ == 2)
                clear();
            escape_ = 0;
            continue;
        }
        if (ch == 27)
        {
            escape_ = 1;
            continue;
        }
        if (ch == '\r')
        {
            cursor_ = 0;
            continue;
        }
        if (ch == '\n')
        {
            newline();
            continue;
        }
        if (ch == '\b')
        {
            if (cursor_)
                --cursor_;
            continue;
        }
        if (ch == '\t')
        {
            do
            {
                feed(" ");
            } while (cursor_ % 8 != 0);
            continue;
        }
        if (ch < 32 || ch == 127)
            continue;
        if (cursor_ == columns)
            newline();
        auto &dest = lines_[(start_ + count_ - 1) % capacity];
        while (dest.size < cursor_)
            dest.text[dest.size++] = ' ';
        dest.text[cursor_] = ch < 127 ? static_cast<char>(ch) : '?';
        dest.colors[cursor_++] = color_;
        if (cursor_ > dest.size)
            dest.size = cursor_;
    }
}
void Editor::insert(std::string_view text) noexcept
{
    for (unsigned char ch : text)
    {
        if (size_ == limit)
            break;
        if (ch < 32 || ch > 126)
            continue;
        for (std::size_t i = size_; i > cursor_; --i)
            text_[i] = text_[i - 1];
        text_[cursor_++] = static_cast<char>(ch);
        ++size_;
    }
}
void Editor::backspace() noexcept
{
    if (cursor_)
    {
        --cursor_;
        erase();
    }
}
void Editor::erase() noexcept
{
    if (cursor_ >= size_)
        return;
    for (std::size_t i = cursor_; i + 1 < size_; ++i)
        text_[i] = text_[i + 1];
    --size_;
}
void Editor::commit() noexcept
{
    if (size_ && (!history_count_ || text() != std::string_view(history_[history_count_ - 1].data(),
                                                                lengths_[history_count_ - 1])))
    {
        if (history_count_ == 32)
        {
            for (std::size_t i = 1; i < 32; ++i)
            {
                history_[i - 1] = history_[i];
                lengths_[i - 1] = lengths_[i];
            }
            --history_count_;
        }
        history_[history_count_] = text_;
        lengths_[history_count_++] = size_;
    }
    history_pos_ = history_count_;
    clear();
}
void Editor::history(int direction) noexcept
{
    if (history_pos_ == history_count_)
    {
        draft_ = text_;
        draft_size_ = size_;
    }
    if (direction < 0 && history_pos_)
        --history_pos_;
    else if (direction > 0 && history_pos_ < history_count_)
        ++history_pos_;
    else
        return;
    if (history_pos_ == history_count_)
    {
        text_ = draft_;
        size_ = draft_size_;
    }
    else
    {
        text_ = history_[history_pos_];
        size_ = lengths_[history_pos_];
    }
    cursor_ = size_;
}
} // namespace term
