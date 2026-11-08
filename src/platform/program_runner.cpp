// PS5Term - run a PS5 program for a tab through the ps5term-run payload. GPL-3.0-or-later.
#include "program_runner.hpp"
#include "../network.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <netinet/in.h>
#include <new>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace term::platform
{
namespace
{
std::uint64_t now_ms() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000 +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000000;
}
int send_flags() noexcept
{
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}
void no_sigpipe([[maybe_unused]] int fd) noexcept
{
#ifdef SO_NOSIGPIPE
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
}
void close_fd(int &fd) noexcept
{
    if (fd >= 0)
        ::close(fd);
    fd = -1;
}
bool write_all(int fd, const char *bytes, std::size_t size) noexcept
{
    while (size)
    {
        const auto wrote = ::write(fd, bytes, size);
        if (wrote < 0 && errno == EINTR)
            continue;
        if (wrote <= 0)
            return false;
        bytes += wrote;
        size -= static_cast<std::size_t>(wrote);
    }
    return true;
}
// The whole launcher, read once; it is a small payload.
unsigned char *read_launcher(const char *path, std::size_t &size) noexcept
{
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return nullptr;
    struct stat st
    {
    };
    unsigned char *bytes = nullptr;
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 &&
        st.st_size <= 4 * 1024 * 1024)
        bytes = new (std::nothrow) unsigned char[static_cast<std::size_t>(st.st_size)];
    std::size_t done = 0;
    while (bytes && done < static_cast<std::size_t>(st.st_size))
    {
        const auto got = ::read(fd, bytes + done, static_cast<std::size_t>(st.st_size) - done);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
        {
            delete[] bytes;
            bytes = nullptr;
            break;
        }
        done += static_cast<std::size_t>(got);
    }
    ::close(fd);
    size = bytes ? done : 0;
    return bytes;
}
// Best effort: a program without ncurses never needs it, and one that does says so itself.
void install_terminfo(const char *source, const char *directory) noexcept
{
    if (!source || !directory || !*directory)
        return;
    char target[512];
    std::snprintf(target, sizeof target, "%s/x/xterm-256color", directory);
    struct stat st
    {
    };
    if (::stat(target, &st) == 0)
        return;
    std::size_t size = 0;
    unsigned char *entry = read_launcher(source, size);
    if (!entry)
        return;
    char folder[512];
    std::snprintf(folder, sizeof folder, "%s", directory);
    ::mkdir(folder, 0755); // Read by the program, which runs as another user.
    ::chmod(folder, 0755);
    std::snprintf(folder, sizeof folder, "%s/x", directory);
    ::mkdir(folder, 0755);
    ::chmod(folder, 0755);
    char partial[540];
    std::snprintf(partial, sizeof partial, "%s.partial", target);
    const int fd = ::open(partial, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    const bool written = fd >= 0 && ::fchmod(fd, 0644) == 0 &&
                         write_all(fd, reinterpret_cast<const char *>(entry), size);
    if (fd >= 0)
        ::close(fd);
    if (!written || ::rename(partial, target))
        ::unlink(partial);
    delete[] entry;
}
} // namespace

ProgramRunner::~ProgramRunner()
{
    close_all();
}
void ProgramRunner::close_all() noexcept
{
    close_fd(listener_);
    close_fd(loader_);
    close_fd(data_);
    close_fd(control_);
    for (auto &pending : pending_)
        close_fd(pending.fd);
    delete[] launcher_;
    launcher_ = nullptr;
    launcher_size_ = launcher_sent_ = 0;
}
void ProgramRunner::fail(const char *reason) noexcept
{
    std::snprintf(failure_.data(), failure_.size(), "%s", reason);
    state_ = State::failed;
    close_all();
}
void ProgramRunner::stop() noexcept
{
    close_all();
    if (state_ != State::finished && state_ != State::failed)
        state_ = State::idle;
}
bool ProgramRunner::start(std::string_view arguments, std::string_view cwd, unsigned columns,
                          unsigned rows) noexcept
{
    close_all();
    failure_[0] = '\0';
    killed_ = false;
    status_ = 0;
    control_size_ = 0;
    state_ = State::failed;
    if (arguments.empty() || arguments[0] != '/' || cwd.empty() || cwd[0] != '/')
    {
        fail("run: give the program's full path");
        return false;
    }
    // A fresh token, so only this launcher's connections are accepted.
    unsigned char random[16];
    const int entropy = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    const bool have_random = entropy >= 0 && ::read(entropy, random, sizeof random) ==
                                                 static_cast<ssize_t>(sizeof random);
    if (entropy >= 0)
        ::close(entropy);
    if (!have_random)
    {
        fail("run: no random source for the launch token");
        return false;
    }
    for (std::size_t i = 0; i < sizeof random; ++i)
        std::snprintf(token_.data() + i * 2, 3, "%02x", random[i]);

    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    auto address = loopback_address(0);
    socklen_t length = sizeof address;
    if (listener_ < 0 || !nonblocking_socket(listener_) ||
        ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address) ||
        ::listen(listener_, 4) ||
        ::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &length))
    {
        fail("run: cannot open a loopback port");
        return false;
    }
    // The request: one key=value per line (tooling/run-payload/run_request.h).
    std::array<char, max_text + 512> request{};
    int size = std::snprintf(request.data(), request.size(),
                             "token=%s\nport=%u\ncolumns=%u\nrows=%u\ncwd=%.*s\n", token_.data(),
                             static_cast<unsigned>(ntohs(address.sin_port)), columns, rows,
                             static_cast<int>(cwd.size()), cwd.data());
    std::size_t count = 0;
    for (std::size_t at = 0; size > 0 && at < arguments.size();)
    {
        auto end = arguments.find('\0', at);
        if (end == std::string_view::npos)
            end = arguments.size();
        const auto argument = arguments.substr(at, end - at);
        if (argument.find('\n') != std::string_view::npos || ++count > max_arguments)
            size = -1;
        else
        {
            const int more =
                std::snprintf(request.data() + size, request.size() - size, "arg=%.*s\n",
                              static_cast<int>(argument.size()), argument.data());
            size = more < 0 || static_cast<std::size_t>(size + more) >= request.size()
                       ? -1
                       : size + more;
        }
        at = end + 1;
    }
    if (size <= 0 || static_cast<std::size_t>(size) > max_text)
    {
        fail("run: too many or too long arguments");
        return false;
    }
    char directory[512];
    std::snprintf(directory, sizeof directory, "%s", config_.request_path);
    // The launcher runs as a payload, not as this app's user: it must be able to read the
    // request (seen on the console: 0600 gave it EACCES). Anything that can read /data on a
    // jailbroken console is privileged already; the token only keeps stray connections out.
    if (char *slash = std::strrchr(directory, '/'); slash && slash != directory)
    {
        *slash = '\0';
        ::mkdir(directory, 0711);
        ::chmod(directory, 0711);
    }
    const int file =
        ::open(config_.request_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    const bool written = file >= 0 && ::fchmod(file, 0644) == 0 &&
                         write_all(file, request.data(), static_cast<std::size_t>(size));
    if (file >= 0)
        ::close(file);
    if (!written)
    {
        fail("run: cannot write /data/ps5term/run/request");
        return false;
    }
    install_terminfo(config_.terminfo_source, config_.terminfo_directory);
    launcher_ = read_launcher(config_.launcher_path, launcher_size_);
    if (!launcher_)
    {
        fail("run: the launcher payload is missing from this build");
        return false;
    }
    loader_ = ::socket(AF_INET, SOCK_STREAM, 0);
    auto loader = loopback_address(config_.loader_port);
    if (loader_ < 0 || !nonblocking_socket(loader_) ||
        (::connect(loader_, reinterpret_cast<sockaddr *>(&loader), sizeof loader) < 0 &&
         errno != EINPROGRESS))
    {
        fail("run: no ELF loader on port 9021. Load elfldr (or any loader on 9021), then try "
             "again.");
        return false;
    }
    no_sigpipe(loader_);
    deadline_ = now_ms() + config_.connect_timeout_ms;
    state_ = State::sending;
    return true;
}
void ProgramRunner::tick() noexcept
{
    if (state_ == State::sending)
    {
        while (launcher_sent_ < launcher_size_)
        {
            const auto sent = ::send(loader_, launcher_ + launcher_sent_,
                                     launcher_size_ - launcher_sent_, send_flags());
            if (sent > 0)
            {
                launcher_sent_ += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 &&
                (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOTCONN || errno == EINTR))
                break;
            fail("run: no ELF loader on port 9021. Load elfldr (or any loader on 9021), then "
                 "try again.");
            return;
        }
        if (launcher_sent_ == launcher_size_)
        {
            close_fd(loader_); // The loader runs what it got when the connection closes.
            delete[] launcher_;
            launcher_ = nullptr;
            state_ = State::waiting;
        }
        else if (now_ms() >= deadline_)
            fail("run: the ELF loader on port 9021 did not answer");
        return;
    }
    if (state_ == State::waiting)
    {
        accept_pending();
        if (control_ >= 0)
            read_control();
        if (state_ == State::waiting && now_ms() >= deadline_)
            fail("run: the launcher did not start (is the loader on 9021 an ELF loader?)");
        return;
    }
    if (state_ == State::running && control_ >= 0)
        read_control();
}
void ProgramRunner::accept_pending() noexcept
{
    for (;;)
    {
        const int fd = ::accept(listener_, nullptr, nullptr);
        if (fd < 0)
            break;
        if (!nonblocking_socket(fd))
        {
            ::close(fd);
            continue;
        }
        no_sigpipe(fd);
        bool placed = false;
        for (auto &pending : pending_)
            if (pending.fd < 0)
            {
                pending = {};
                pending.fd = fd;
                placed = true;
                break;
            }
        if (!placed)
            ::close(fd);
    }
    // Each connection's first line names it and carries the token; anything else is dropped.
    for (auto &pending : pending_)
    {
        if (pending.fd < 0)
            continue;
        while (pending.size < pending.line.size() - 1)
        {
            const auto got = ::recv(pending.fd, pending.line.data() + pending.size, 1, 0);
            if (got <= 0)
            {
                if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                    close_fd(pending.fd);
                break;
            }
            if (pending.line[pending.size] == '\n')
            {
                const std::string_view line(pending.line.data(), pending.size);
                const auto space = line.find(' ');
                const auto kind = line.substr(0, space);
                const bool valid = space != std::string_view::npos &&
                                   line.substr(space + 1) == std::string_view(token_.data());
                int &slot = kind == "data" ? data_ : control_;
                if (valid && (kind == "data" || kind == "control") && slot < 0)
                {
                    slot = pending.fd;
                    pending.fd = -1;
                }
                else
                    close_fd(pending.fd);
                pending.size = 0;
                break;
            }
            ++pending.size;
        }
        if (pending.fd >= 0 && pending.size >= pending.line.size() - 1)
            close_fd(pending.fd);
    }
    if (data_ >= 0 && control_ >= 0)
        close_fd(listener_);
}
void ProgramRunner::read_control() noexcept
{
    for (;;)
    {
        if (control_size_ >= control_line_.size() - 1)
            control_size_ = 0; // The launcher never sends lines this long.
        const auto got = ::recv(control_, control_line_.data() + control_size_,
                                control_line_.size() - 1 - control_size_, 0);
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        if (got <= 0)
        {
            close_fd(control_);
            if (state_ == State::running)
            {
                // The launcher went away without a word: the program is gone with it.
                state_ = State::finished;
                killed_ = true;
                status_ = 9;
            }
            else if (state_ == State::waiting)
                fail("run: the launcher stopped before the program started");
            return;
        }
        control_size_ += static_cast<std::size_t>(got);
        for (char *end;
             control_size_ &&
             (end = static_cast<char *>(std::memchr(control_line_.data(), '\n', control_size_)));)
        {
            const auto length = static_cast<std::size_t>(end - control_line_.data());
            control_message({control_line_.data(), length});
            if (control_ < 0)
                return;
            control_size_ -= length + 1;
            std::memmove(control_line_.data(), end + 1, control_size_);
        }
    }
}
void ProgramRunner::control_message(std::string_view line) noexcept
{
    const auto space = line.find(' ');
    const auto word = line.substr(0, space);
    const auto rest = space == std::string_view::npos ? std::string_view{} : line.substr(space + 1);
    int number = 0;
    bool negative = !rest.empty() && rest[0] == '-';
    for (char ch : rest.substr(negative ? 1 : 0))
        if (ch >= '0' && ch <= '9' && number < 100000000)
            number = number * 10 + (ch - '0');
    if (negative)
        number = -number;
    if (word == "started" && state_ == State::waiting && data_ >= 0)
        state_ = State::running;
    else if (word == "failed" && state_ == State::waiting)
    {
        char reason[160];
        std::snprintf(reason, sizeof reason, "run: %.*s", static_cast<int>(rest.size()),
                      rest.data());
        fail(reason);
    }
    else if ((word == "exited" || word == "killed") &&
             (state_ == State::running || state_ == State::waiting))
    {
        killed_ = word == "killed";
        status_ = number;
        state_ = State::finished;
        close_fd(control_);
    }
}
IoResult ProgramRunner::read(char *bytes, std::size_t size) noexcept
{
    if (data_ < 0)
        return {0,
                state_ == State::finished || state_ == State::failed ? IoState::eof
                                                                     : IoState::would_block,
                0};
    const auto got = ::recv(data_, bytes, size, 0);
    if (got > 0)
        return {static_cast<std::size_t>(got), IoState::ok, 0};
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return {0, IoState::would_block, 0};
    // The program closed its output (it ended, or closed stdout): nothing more will come.
    close_fd(data_);
    return {0, state_ == State::finished ? IoState::eof : IoState::would_block, 0};
}
IoResult ProgramRunner::write(const char *bytes, std::size_t size) noexcept
{
    if (data_ < 0 || state_ != State::running)
        return {size, IoState::ok, 0}; // Typing after the end goes nowhere, as on a TTY.
    const auto sent = ::send(data_, bytes, size, send_flags());
    if (sent >= 0)
        return {static_cast<std::size_t>(sent), sent ? IoState::ok : IoState::would_block, 0};
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return {0, IoState::would_block, 0};
    return {size, IoState::ok, 0}; // A program that stopped reading; its end follows.
}
bool ProgramRunner::interrupt() noexcept
{
    if (control_ < 0 || state_ != State::running)
        return false;
    return ::send(control_, "int\n", 4, send_flags()) == 4;
}
} // namespace term::platform
