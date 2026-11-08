// PS5Term - GPL-3.0-or-later.
#include "command_session.hpp"
#include <algorithm>
namespace term::ssh
{
template <std::size_t Capacity>
void CommandSession::Queue<Capacity>::push(std::string_view value) noexcept
{
    // A caller must reserve the whole write. Keep this invariant enforced on
    // PS5 too, whose minimal runtime does not export the libc assert helper.
    if (value.size() > space())
        __builtin_trap();
    for (char c : value)
    {
        bytes[(head + size) % Capacity] = c;
        ++size;
    }
}
template <std::size_t Capacity>
std::size_t CommandSession::Queue<Capacity>::read(char *destination, std::size_t capacity) noexcept
{
    if (!destination)
        return 0;
    const auto count = std::min(size, capacity);
    for (std::size_t i = 0; i < count; ++i)
    {
        destination[i] = bytes[head];
        bytes[head] = 0;
        head = (head + 1) % Capacity;
    }
    size -= count;
    return count;
}
CommandSession::CommandSession(SessionMode mode, std::string_view command,
                               std::string_view directory) noexcept
    : mode_(mode), shell_(directory, mode == SessionMode::shell)
{
    running_ = shell_.submit(mode == SessionMode::exec ? command : std::string_view{});
    if (!running_)
    {
        errors_.push("Invalid or overlong command.\n");
        status_ = 2;
        ending_ = true;
    }
}
std::size_t CommandSession::write(std::string_view bytes) noexcept
{
    if (ending_)
        return 0;
    // No command here reads exec stdin. Discard it rather than stall the transport.
    if (mode_ != SessionMode::shell)
        return bytes.size();
    // Ctrl-C must work while a command runs, the queue is full, or after stdin EOF.
    std::size_t consumed = 0;
    if (const auto last = bytes.rfind('\x03'); last != bytes.npos)
    {
        input_.clear();
        interrupt_ = true;
        eof_ = false;
        consumed = last + 1;
        bytes.remove_prefix(consumed);
    }
    if (eof_)
        return consumed + bytes.size();
    const auto count = std::min(bytes.size(), input_.space());
    input_.push(bytes.substr(0, count));
    return consumed + count;
}
void CommandSession::submit_line() noexcept
{
    output_.push("\r\n");
    if (overflow_ || invalid_)
    {
        errors_.push(overflow_ ? "Command too long; nothing executed.\n"
                               : "Invalid command bytes; nothing executed.\n");
        completion_override_ = 2;
        running_ = shell_.submit("");
    }
    else
    {
        const std::string_view line(line_.data(), length_);
        std::string_view word = line;
        while (word.starts_with(' ') || word.starts_with('\t'))
            word.remove_prefix(1);
        while (word.ends_with(' ') || word.ends_with('\t'))
            word.remove_suffix(1);
        if (word == "exit" || word.starts_with("exit ") || word.starts_with("exit\t"))
        {
            // Plain exit reports the last command's status, like a POSIX shell.
            word.remove_prefix(4);
            while (word.starts_with(' ') || word.starts_with('\t'))
                word.remove_prefix(1);
            int code = 0;
            bool valid = !word.empty();
            for (char ch : word)
            {
                valid = valid && ch >= '0' && ch <= '9';
                if (valid && code < 1000)
                    code = code * 10 + (ch - '0');
            }
            if (valid)
                status_ = code & 255;
            else if (!word.empty())
                status_ = 2;
            eof_ = true;
            ending_ = true;
            input_.clear();
        }
        else
            running_ = shell_.submit(line);
    }
    line_ = {};
    length_ = 0;
    overflow_ = false;
    invalid_ = false;
}
void CommandSession::tick() noexcept
{
    if (ending_)
        return;
    // Backpressure, but an interrupt must not wait behind output it will discard.
    if (!interrupt_ && (output_.space() < 4100 || errors_.space() < 512))
        return;
    if (interrupt_ && (output_.space() < 8 || errors_.space() < 512))
        return;
    if (interrupt_)
    {
        interrupt_ = false;
        line_ = {};
        length_ = 0;
        overflow_ = false;
        invalid_ = false;
        escape_ = 0;
        cr_ = false;
        shell_.interrupt();
        output_.push("^C\r\n");
        status_ = 130;
        completion_override_ = 130;
        if (!running_)
            running_ = shell_.submit("");
        if (output_.space() < 4100 || errors_.space() < 512)
            return;
    }
    if (running_)
    {
        shell_.tick();
        char bytes[4096];
        auto count = shell_.read(bytes, sizeof bytes);
        output_.push({bytes, count});
        count = shell_.read_error(bytes, 512);
        errors_.push({bytes, count});
        if (shell_.busy())
            return;
        running_ = false;
        status_ = completion_override_ >= 0 ? completion_override_ : shell_.exit_status();
        completion_override_ = -1;
        if (mode_ == SessionMode::exec)
        {
            ending_ = true;
            return;
        }
    }
    if (output_.space() < 4096 || errors_.space() < 512)
        return;
    for (unsigned i = 0; i < 512 && input_.size; ++i)
    {
        char c = 0;
        input_.read(&c, 1);
        if (cr_ && c == '\n')
        {
            cr_ = false;
            continue;
        }
        cr_ = false;
        if (escape_)
        {
            // Drop bounded ANSI cursor-key sequences rather than injecting
            // their suffix into the command. No history/PTY emulation here.
            // A line ending always ends the sequence: an unfinished escape must
            // not join two commands into one.
            if (c == '\r' || c == '\n')
                escape_ = 0;
            else
            {
                if (escape_ == 1 && (c == '[' || c == 'O'))
                    escape_ = 2;
                else if (escape_ == 1 || (c >= '@' && c <= '~') || ++escape_ > 32)
                    escape_ = 0;
                continue;
            }
        }
        if (c == 27)
        {
            escape_ = 1;
            continue;
        }
        if (c == '\r' || c == '\n')
        {
            cr_ = c == '\r';
            submit_line();
            return;
        }
        if (c == 4)
        {
            if (length_ || overflow_ || invalid_)
            {
                submit_line();
                return;
            }
            eof_ = true;
            input_.clear();
            break;
        }
        if (c == 8 || c == 127)
        {
            if (length_ && !overflow_)
            {
                // Remove a whole UTF-8 character, never half of one.
                while (length_ > 1 &&
                       (static_cast<unsigned char>(line_[length_ - 1]) & 0xc0) == 0x80)
                    line_[--length_] = 0;
                line_[--length_] = 0;
                output_.push("\b \b");
            }
            continue;
        }
        if (c == 0)
        {
            invalid_ = true;
            continue;
        }
        if ((static_cast<unsigned char>(c) < 32 && c != '\t') || c == 127)
            continue;
        if (length_ == 512)
            overflow_ = true;
        if (!overflow_)
            line_[length_++] = c;
        output_.push({&c, 1});
    }
    if (eof_ && !input_.size && !running_)
    {
        if (length_ || overflow_ || invalid_)
            submit_line();
        else
            ending_ = true;
    }
}
std::size_t CommandSession::read_stdout(char *destination, std::size_t capacity) noexcept
{
    return output_.read(destination, capacity);
}
std::size_t CommandSession::read_stderr(char *destination, std::size_t capacity) noexcept
{
    return errors_.read(destination, capacity);
}
bool CommandSession::finished() const noexcept
{
    return ending_ && !output_.size && !errors_.size;
}
void CommandSession::close() noexcept
{
    shell_.reset();
    input_.clear();
    output_.clear();
    errors_.clear();
    line_ = {};
    length_ = 0;
    running_ = false;
    ending_ = true;
    eof_ = true;
    status_ = 130;
}
} // namespace term::ssh
