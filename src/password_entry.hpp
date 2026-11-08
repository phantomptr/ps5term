// PS5Term password input, deliberately separate from terminal/history/clipboard.
// GPL-3.0-or-later.
#pragma once
#include <array>
#include <string_view>
namespace term
{
class PasswordEntry
{
  public:
    PasswordEntry() = default;
    ~PasswordEntry()
    {
        cancel();
    }
    PasswordEntry(const PasswordEntry &) = delete;
    PasswordEntry &operator=(const PasswordEntry &) = delete;
    void begin() noexcept
    {
        cancel();
        active_ = true;
    }
    void cancel() noexcept
    {
        wipe(first_);
        wipe(input_);
        size_ = cursor_ = first_size_ = 0;
        active_ = confirming_ = false;
        message_ = "8-64 printable characters. Spaces are preserved.";
    }
    bool active() const noexcept
    {
        return active_;
    }
    bool confirming() const noexcept
    {
        return confirming_;
    }
    std::size_t size() const noexcept
    {
        return size_;
    }
    std::size_t cursor() const noexcept
    {
        return cursor_;
    }
    std::string_view value() const noexcept
    {
        return {first_.data(), first_size_};
    }
    const char *message() const noexcept
    {
        return message_;
    }
    void failed() noexcept
    {
        message_ = "Cannot save password. Check SSH identity storage and retry.";
    }
    void insert(char c) noexcept
    {
        if (!active_ || c < 32 || c > 126 || size_ == input_.size())
            return;
        for (auto i = size_; i > cursor_; --i)
            input_[i] = input_[i - 1];
        input_[cursor_++] = c;
        ++size_;
    }
    void backspace() noexcept
    {
        if (cursor_)
        {
            --cursor_;
            erase();
        }
    }
    void erase() noexcept
    {
        if (cursor_ >= size_)
            return;
        for (auto i = cursor_; i + 1 < size_; ++i)
            input_[i] = input_[i + 1];
        input_[--size_] = 0;
    }
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
    // True only when both entries match. Caller persists then cancels to wipe.
    bool advance() noexcept
    {
        if (!active_)
            return false;
        if (!confirming_)
        {
            if (size_ < 8)
            {
                message_ = "Use at least 8 characters.";
                return false;
            }
            first_ = input_;
            first_size_ = size_;
            confirming_ = true;
            wipe(input_);
            size_ = cursor_ = 0;
            message_ = "Enter the same password again.";
            return false;
        }
        unsigned difference = static_cast<unsigned>(size_ ^ first_size_);
        for (std::size_t i = 0; i < input_.size(); ++i)
            difference |= static_cast<unsigned char>(input_[i] ^ first_[i]);
        if (difference)
        {
            wipe(input_);
            size_ = cursor_ = 0;
            message_ = "Passwords differ. Re-enter the confirmation.";
            return false;
        }
        return true;
    }

  private:
    static void wipe(std::array<char, 64> &buffer) noexcept
    {
        volatile char *p = buffer.data();
        for (std::size_t i = 0; i < buffer.size(); ++i)
            p[i] = 0;
    }
    std::array<char, 64> first_{}, input_{};
    std::size_t size_ = 0, cursor_ = 0, first_size_ = 0;
    bool active_ = false, confirming_ = false;
    const char *message_ = "";
};
} // namespace term
