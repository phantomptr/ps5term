// PS5Term - owned SSH command-channel adapter. GPL-3.0-or-later.
#pragma once
#include "../builtin_shell.hpp"
#include <array>
#include <cstddef>
#include <string_view>
namespace term::ssh
{
enum class SessionMode
{
    shell,
    exec
};
// Transport independent. One owner calls all methods; no threads or sockets.
// No PTY, raw application input, resize, forwarding or child-process claims.
class CommandSession
{
  public:
    CommandSession(SessionMode, std::string_view command = {},
                   std::string_view directory = "/data") noexcept;
    CommandSession(const CommandSession &) = delete;
    CommandSession &operator=(const CommandSession &) = delete;
    // Returns the consumed prefix; a transport must retain and retry the tail.
    std::size_t write(std::string_view) noexcept;
    void finish_input() noexcept
    {
        eof_ = true;
    }
    // A transport's signal request. An exec channel's stdin is data, so a 0x03
    // byte there must never be mistaken for this.
    void interrupt() noexcept
    {
        if (!ending_)
            interrupt_ = true;
    }
    void tick() noexcept;
    std::size_t read_stdout(char *, std::size_t) noexcept;
    std::size_t read_stderr(char *, std::size_t) noexcept;
    bool finished() const noexcept;
    int exit_status() const noexcept
    {
        return status_;
    }
    void close() noexcept;

  private:
    template <std::size_t Capacity> struct Queue
    {
        std::array<char, Capacity> bytes{};
        std::size_t head = 0, size = 0;
        std::size_t space() const noexcept
        {
            return Capacity - size;
        }
        void push(std::string_view) noexcept;
        std::size_t read(char *, std::size_t) noexcept;
        void clear() noexcept
        {
            head = 0;
            size = 0;
        }
    };
    SessionMode mode_;
    BuiltinShell shell_;
    Queue<4096> input_{};
    Queue<8192> output_{};
    Queue<1024> errors_{};
    std::array<char, 513> line_{};
    std::size_t length_ = 0;
    bool running_ = false, eof_ = false, ending_ = false, overflow_ = false, invalid_ = false;
    bool cr_ = false, interrupt_ = false;
    unsigned escape_ = 0;
    int status_ = 0;
    int completion_override_ = -1;
    void submit_line() noexcept;
};
} // namespace term::ssh
