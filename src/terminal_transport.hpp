// PS5Term - UI-thread-owned bounded terminal transport. GPL-3.0-or-later.
#pragma once
#include "terminal_model.hpp"
#include "platform/launcher.hpp"
#include <array>
namespace term
{
class TerminalTransport
{
  public:
    static constexpr std::size_t input_capacity = 64 * 1024, pump_budget = 16 * 1024;
    struct Port
    {
        void *context = nullptr;
        platform::IoResult (*write)(void *, const char *, std::size_t) noexcept = nullptr;
        platform::IoResult (*read)(void *, char *, std::size_t) noexcept = nullptr;
    };
    // Atomically accept an entire write or none; retained until the peer accepts.
    bool write(std::string_view bytes) noexcept;
    void pump(TerminalModel &, Port) noexcept;
    void reset() noexcept;
    bool eof() const noexcept
    {
        return eof_;
    }
    bool failed() const noexcept
    {
        return failed_;
    }

  private:
    std::array<char, input_capacity> input_{};
    std::array<char, 4096> output_{};
    std::size_t input_size_ = 0, input_sent_ = 0, output_size_ = 0, output_fed_ = 0;
    bool eof_ = false, failed_ = false;
    void flush(TerminalModel &, Port, std::size_t &) noexcept;
};
} // namespace term
