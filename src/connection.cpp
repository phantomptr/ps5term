// PS5Term - GPL-3.0-or-later.
#include "connection.hpp"
#include "i18n.hpp"
#include "network.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
namespace term
{
#if !defined(PS5TERM_LEGACY_SHELL_RESEARCH)
Connection::~Connection()
{
    if (guest_peer_ && guest_handle_)
        guest_peer_->close(guest_handle_);
    if (native_ && pty_child_.valid())
    {
        pty_launcher_.stop(pty_child_);
    }
    if (!borrowed_launcher_)
        platform::pty_launcher_destroy(pty_api_);
    linux_vm_.stop();
}
bool Connection::select_environment(Environment environment) noexcept
{
    if (guest_peer_)
        return false; // VM lifecycle belongs to the primary connection.
    if (environment == Environment::linux_guest && !linux_available())
        return false;
    if (!linux_vm_.stop())
    {
        status_ = linux_vm_.status();
        return false;
    }
    reconnect();
    environment_ = environment;
    status_ = environment == Environment::linux_guest ? N_("Starting Linux guest")
                                                      : N_("Starting console shell");
    return true;
}
void Connection::reconnect() noexcept
{
    program_.stop(); // The launcher stops a running program when its control closes.
    program_mode_ = program_raw_ = false;
    if (guest_peer_ && guest_handle_)
    {
        guest_peer_->close(guest_handle_);
        guest_handle_ = 0;
    }
    if (!linux_vm_.stop())
    {
        status_ = linux_vm_.status();
        return;
    }
    if (native_ && pty_child_.valid())
    {
        pty_launcher_.stop(pty_child_);
        pty_child_ = {};
    }
    native_ = false;
    linux_ = false;
    builtin_.reset();
    transport_.reset();
    state_ = State::idle;
    status_ = N_("Starting shell");
}
bool Connection::submit(std::string_view command) noexcept
{
    if (!connected())
        return false;
    if (native_ || linux_ || program_mode_)
    {
        std::array<char, 4096> buf{};
        if (command.size() + 1 > buf.size())
            return false;
        std::memcpy(buf.data(), command.data(), command.size());
        buf[command.size()] = '\n';
        return transport_.write({buf.data(), command.size() + 1});
    }
    return builtin_.submit(command);
}
bool Connection::write_raw(const char *data, std::size_t size) noexcept
{
    if (!connected() || !(native_ || linux_ || program_mode_) || !size)
        return false;
    return data && transport_.write({data, size});
}
bool Connection::interrupt() noexcept
{
    if (!connected())
        return false;
    if (program_mode_) // SIGINT, as a TTY would send on Ctrl+C; or give up a slow launch.
        return program_.interrupt() || program_.cancel();
    if (native_ || linux_)
    {
        const char etx = '\x03';
        return write_raw(&etx, 1);
    }
    return builtin_.interrupt();
}
bool Connection::resize(unsigned cols, unsigned rows) noexcept
{
    if (native_ && pty_child_.valid())
        return pty_launcher_.resize(pty_child_, cols, rows);
    if (guest_peer_)
        return guest_handle_ && guest_peer_->resize(guest_handle_, cols, rows);
    if (linux_)
        return linux_vm_.resize(cols, rows);
    return false;
}
bool Connection::try_native(TerminalModel &model) noexcept
{
    if (!pty_api_.context)
        pty_api_ = platform::pty_launcher_create();
    if (!pty_api_.context)
        return false;
    pty_launcher_ = platform::PlatformLauncher(pty_api_);
    platform::LaunchSpec spec{};
    spec.program = "/bin/sh";
    spec.generation = 1;
    auto result = pty_launcher_.start(spec);
    if (!result.child.valid())
    {
        char msg[128];
        std::snprintf(msg, sizeof msg, "Native launch failed (errno %d). Falling back.\r\n",
                      result.native_error);
        model.feed(msg);
        if (!borrowed_launcher_)
            platform::pty_launcher_destroy(pty_api_);
        return false;
    }
    pty_child_ = result.child;
    native_ = true;
    model.feed("\033[20l"); // PTY supplies its own line discipline.
    model.feed("Native PTY shell ready.\r\n");
    return true;
}
void Connection::tick_native(TerminalModel &model) noexcept
{
    (void)pty_launcher_.poll(pty_child_); // Reap, but keep the output handle until EOF.
    transport_.pump(model, {this, transport_write, transport_read});
    if (transport_.eof() || transport_.failed())
    {
        model.feed("\r\n[shell exited]\r\n");
        state_ = State::failed;
        status_ = N_("Shell exited");
        return;
    }
}
platform::IoResult Connection::transport_write(void *context, const char *bytes,
                                               std::size_t size) noexcept
{
    auto &self = *static_cast<Connection *>(context);
    if (self.program_mode_)
    {
        // No TTY turns Ctrl+C into a signal for the program, so PS5Term does: the byte becomes
        // SIGINT and is not passed on.
        const char *interrupt = static_cast<const char *>(std::memchr(bytes, '\x03', size));
        if (interrupt == bytes)
        {
            if (!self.program_.interrupt())
                self.program_.cancel();
            return {1, platform::IoState::ok, 0};
        }
        if (interrupt)
            size = static_cast<std::size_t>(interrupt - bytes);
        // Enter is CR from the keyboard; a TTY would hand the program LF (ICRNL).
        const char *enter = static_cast<const char *>(std::memchr(bytes, '\r', size));
        if (enter == bytes)
        {
            auto result = self.program_.write("\n", 1);
            return {result.count, result.state, result.native_error};
        }
        if (enter)
            size = static_cast<std::size_t>(enter - bytes);
        return self.program_.write(bytes, size);
    }
    if (self.native_)
        return self.pty_launcher_.write(self.pty_child_, bytes, size);
    if (!self.linux_)
        return {size, platform::IoState::ok, 0}; // No peer for basic-mode VT replies.
    auto n = self.guest_peer_ ? self.guest_peer_->write(self.guest_handle_, {bytes, size})
                              : self.linux_vm_.write(bytes, size);
    return {n, n ? platform::IoState::ok : platform::IoState::would_block, 0};
}
platform::IoResult Connection::transport_read(void *context, char *bytes, std::size_t size) noexcept
{
    auto &self = *static_cast<Connection *>(context);
    if (self.program_mode_)
        return self.program_.read(bytes, size);
    if (self.native_)
        return self.pty_launcher_.read(self.pty_child_, bytes, size);
    if (self.guest_peer_)
    {
        const auto n = self.guest_peer_->read(self.guest_handle_, bytes, size);
        const auto state = self.guest_peer_->state(self.guest_handle_);
        const bool done = state == platform::GuestAgent::State::gone ||
                          state == platform::GuestAgent::State::exited;
        return {n,
                n      ? platform::IoState::ok
                : done ? platform::IoState::eof
                       : platform::IoState::would_block,
                0};
    }
    auto n = self.linux_ ? self.linux_vm_.read(bytes, size) : self.builtin_.read(bytes, size);
    auto state = n                                          ? platform::IoState::ok
                 : self.linux_ && !self.linux_vm_.running() ? platform::IoState::eof
                                                            : platform::IoState::would_block;
    return {n, state, 0};
}
bool Connection::try_linux(TerminalModel &model) noexcept
{
    auto cols = static_cast<unsigned>(model.columns());
    auto rows = static_cast<unsigned>(model.rows());
    if (!linux_vm_.start(cols, rows))
        return false;
    linux_ = true;
    model.feed("\033[20l"); // Guest tty owns CR/LF processing.
    model.feed("Linux guest booting (RISC-V / TinyEMU)...\r\n"
               "Persistent files: /mnt/data. PS5 files: /mnt/ps5. SSH login: linux.\r\n");
    return true;
}
void Connection::tick_linux(TerminalModel &model) noexcept
{
    if (!guest_peer_)
        linux_vm_.agent().pump();
    transport_.pump(model, {this, transport_write, transport_read});
    if (guest_peer_ && guest_peer_->alive() && transport_.eof())
    {
        // Only this tab's shell ended (exit); Linux itself still runs.
        model.feed("\r\n[Linux shell ended]\r\n");
        state_ = State::failed;
        status_ = N_("Linux shell ended; Reconnect opens a new one");
        return;
    }
    if (transport_.eof() || transport_.failed())
    {
        model.feed("\r\n[Linux guest stopped]\r\n");
        state_ = State::failed;
        status_ = N_("Linux guest stopped; Reconnect retries");
        if (!guest_peer_)
            linux_vm_.stop();
        return;
    }
}
void Connection::tick(TerminalModel &model, std::uint64_t now) noexcept
{
    if (state_ == State::failed)
        return;
    if (state_ == State::idle)
    {
        if (guest_peer_)
        {
            guest_handle_ =
                guest_peer_->alive()
                    ? guest_peer_->open(static_cast<unsigned>(model.columns()),
                                        static_cast<unsigned>(model.rows()), "xterm-256color",
                                        guest_command_ ? guest_command_ : "",
                                        guest_command_ != nullptr)
                    : 0;
            if (!guest_handle_)
            {
                state_ = State::failed;
                status_ =
                    guest_peer_->alive()
                        ? N_("Linux shell slots full; close a tab or SSH session, then Reconnect")
                        : N_("Linux is stopped or starting; Reconnect when ready");
                model.feed(status_);
                return;
            }
            linux_ = true;
            state_ = State::ready;
            status_ = N_("Independent Linux shell");
            model.feed("\033[20l");
            tick_linux(model);
            return;
        }
        else if (environment_ == Environment::linux_guest)
        {
            if (try_linux(model))
            {
                state_ = State::ready;
                status_ = N_("Linux guest running");
            }
            else
            {
                state_ = State::failed;
                status_ = linux_vm_.status();
                model.feed(linux_vm_.status());
                model.feed(
                    "\r\nSettings: Stop Linux returns to the console. Reconnect retries.\r\n");
            }
            return;
        }
        model.feed("\033[20h"); // Basic output and launch diagnostics use LF-only lines.
        if (try_native(model))
        {
            state_ = State::ready;
            status_ = N_("Native shell ready");
            resize(static_cast<unsigned>(model.columns()), static_cast<unsigned>(model.rows()));
            return;
        }
        model.feed("Built-in console ready. Type help for commands.\n"
                   "Basic command mode; no PTY or external loader required.\n\n");
        builtin_.submit("");
        state_ = State::ready;
        status_ = N_("Built-in shell ready");
    }
    if (native_)
    {
        tick_native(model);
        return;
    }
    if (linux_)
    {
        tick_linux(model);
        return;
    }
    if (program_mode_)
    {
        tick_program(model, now);
        return;
    }
    builtin_.tick();
    transport_.pump(model, {this, transport_write, transport_read});
    if (builtin_.program_requested())
        start_program(model);
    // Printing a file can reset the terminal (ESC c); basic output uses LF-only lines.
    model.feed("\033[20h");
}
// `run PATH`: the program gets this tab until it ends; then the PS5 shell prompt returns.
void Connection::start_program(TerminalModel &model) noexcept
{
    builtin_.program_taken();
    if (!program_.start(builtin_.program_arguments(), builtin_.directory(),
                        static_cast<unsigned>(model.columns()),
                        static_cast<unsigned>(model.rows())))
    {
        char message[200];
        std::snprintf(message, sizeof message, "%s\n", program_.failure());
        builtin_.program_finished(message, 127);
        return;
    }
    transport_.reset();
    model.feed("\033[20h"); // A program without a TTY writes bare LF line ends.
    program_mode_ = true;
    program_raw_ = false;
    program_ended_at_ = 0;
}
void Connection::tick_program(TerminalModel &model, std::uint64_t now) noexcept
{
    program_.tick();
    if (program_.state() == platform::ProgramRunner::State::failed)
    {
        finish_program(model);
        return;
    }
    // Never feed anything of PS5Term's own between the program's bytes: its escape
    // sequences can arrive split across reads (seen on the console: ESC[?1049h cut in two
    // never switched screens). Newline mode is set once, in start_program().
    transport_.pump(model, {this, transport_write, transport_read});
    program_raw_ = model.alternate_screen();
    status_ = program_raw_ ? N_("PS5 program running full screen: keys go to it")
                           : N_("PS5 program running: Enter sends a line, Ctrl+C interrupts");
    if (program_.state() == platform::ProgramRunner::State::finished)
    {
        if (!program_ended_at_)
            program_ended_at_ = now ? now : 1;
        // Its last output can trail the exit report a little; a child it left running may
        // hold the output open, so do not wait for ever.
        if (program_.output_closed() || transport_.eof() || now - program_ended_at_ > 1000000)
            finish_program(model);
    }
}
void Connection::finish_program(TerminalModel &model) noexcept
{
    if (model.alternate_screen())
        model.feed("\033[?1049l"); // A program that ended mid-screen leaves the shell readable.
    model.feed("\033[0m\033[20h");
    const auto arguments = builtin_.program_arguments();
    auto name = arguments.substr(0, arguments.find('\0'));
    if (const auto slash = name.rfind('/'); slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    char message[240];
    int status = 127;
    if (program_.state() == platform::ProgramRunner::State::failed)
        std::snprintf(message, sizeof message, "\n%s\n", program_.failure());
    else if (program_.killed())
    {
        status = 128 + program_.status();
        std::snprintf(message, sizeof message, "\n[%.*s stopped by signal %d]\n",
                      static_cast<int>(name.size()), name.data(), program_.status());
    }
    else
    {
        status = program_.status();
        std::snprintf(message, sizeof message, "\n[%.*s exited with status %d]\n",
                      static_cast<int>(name.size()), name.data(), status);
    }
    program_.stop();
    program_mode_ = program_raw_ = false;
    transport_.reset();
    status_ = N_("Built-in shell ready");
    builtin_.program_finished(message, status);
}
#else
Connection::~Connection()
{
    close_socket();
    if (file_ >= 0)
        ::close(file_);
}
void Connection::close_socket() noexcept
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    queued_ = sent_ = 0;
}
void Connection::reconnect() noexcept
{
    close_socket();
    if (file_ >= 0)
        ::close(file_);
    file_ = -1;
    state_ = State::idle;
    launched_ = false;
    telnet_ = Telnet{};
}
int Connection::connect_port(unsigned port) noexcept
{
    close_socket();
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0)
        return -1;
    if (!nonblocking_socket(fd_))
    {
        close_socket();
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int yes = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    auto addr = loopback_address(port);
    int result = ::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (result < 0 && errno != EINPROGRESS)
    {
        close_socket();
        return -1;
    }
    return 0;
}
bool Connection::queue(std::string_view bytes) noexcept
{
    if (bytes.size() > outgoing_.size() - queued_)
        return false;
    for (char ch : bytes)
        outgoing_[queued_++] = ch;
    return true;
}
bool Connection::flush() noexcept
{
    while (sent_ < queued_)
    {
        auto n = ::send(fd_, outgoing_.data() + sent_, queued_ - sent_,
#ifdef MSG_NOSIGNAL
                        MSG_NOSIGNAL
#else
                        0
#endif
        );
        if (n > 0)
            sent_ += static_cast<std::size_t>(n);
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return true;
        else
            return false;
    }
    queued_ = sent_ = 0;
    return true;
}
bool Connection::submit(std::string_view command) noexcept
{
    if (!connected())
        return false;
    auto p = Telnet::encode(command);
    if (p.size + 2 > outgoing_.size() - queued_)
        return false;
    queue(p.view());
    queue("\r\n");
    return true;
}
bool Connection::interrupt() noexcept
{
    return connected() && queue(std::string_view("\xff\xf4", 2));
}
void Connection::fail(TerminalModel &model, std::string_view reason) noexcept
{
    close_socket();
    if (file_ >= 0)
        ::close(file_);
    file_ = -1;
    state_ = State::failed;
    status_ = reason;
    model.feed("\n[PS5Term] ");
    model.feed(reason);
    model.feed("\nStart elfldr on port 9021, then press Cross or F5 to retry.\n");
}
void Connection::tick(TerminalModel &model, std::uint64_t now) noexcept
{
    if (state_ == State::failed)
        return;
    if (state_ == State::idle)
    {
        status_ = N_("Connecting to local shell");
        deadline_ = now + 2000000;
        if (connect_port(2324) < 0)
        {
            state_ = State::waiting;
            next_ = now;
        }
        else
            state_ = State::connecting;
    }
    if (state_ == State::waiting)
    {
        if (now < next_)
            return;
        if (!launched_)
        {
            launched_ = true;
            status_ = N_("Starting bundled shell via elfldr");
            if (connect_port(9021) < 0)
            {
                fail(screen, N_("ELF loader unavailable"));
                return;
            }
            state_ = State::loader_connect;
            deadline_ = now + 3000000;
        }
        else
        {
            if (now > deadline_)
            {
                fail(screen, N_("Shell did not become ready"));
                return;
            }
            if (connect_port(2324) < 0)
            {
                next_ = now + 250000;
                return;
            }
            state_ = State::connecting;
        }
    }
    if (state_ == State::connecting || state_ == State::loader_connect)
    {
        pollfd p{fd_, POLLOUT, 0};
        int ready = ::poll(&p, 1, 0);
        if (ready <= 0)
        {
            if (now > deadline_)
                fail(screen, N_("Connection timed out"));
            return;
        }
        int error = 0;
        socklen_t size = sizeof(error);
        bool ok = ::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
        if (!ok)
        {
            if (state_ == State::loader_connect)
            {
                fail(screen, N_("ELF loader refused the connection"));
                return;
            }
            close_socket();
            state_ = State::waiting;
            next_ = now + 250000;
            return;
        }
        if (state_ == State::connecting)
        {
            state_ = State::ready;
            status_ = N_("Local shell connected");
        }
        else
        {
            file_ = ::open("/app0/assets/backend/ps5term-sh.elf", O_RDONLY);
            if (file_ < 0)
            {
                fail(screen, N_("Bundled shell payload missing"));
                return;
            }
            state_ = State::uploading;
            deadline_ = now + 15000000;
        }
    }
    if (state_ == State::uploading)
    {
        if (now > deadline_)
        {
            fail(screen, N_("Shell upload timed out"));
            return;
        }
        if (!flush())
        {
            fail(screen, N_("Shell upload failed"));
            return;
        }
        if (queued_)
            return;
        auto n = ::read(file_, outgoing_.data(), outgoing_.size());
        if (n > 0)
        {
            queued_ = static_cast<std::size_t>(n);
            return;
        }
        if (n < 0)
        {
            fail(screen, N_("Cannot read shell payload"));
            return;
        }
        ::close(file_);
        file_ = -1;
        ::shutdown(fd_, SHUT_WR);
        close_socket();
        state_ = State::waiting;
        deadline_ = now + 15000000;
        next_ = now + 500000;
        status_ = N_("Waiting for shell startup");
        return;
    }
    if (state_ != State::ready)
        return;
    if (!flush())
    {
        fail(screen, N_("Shell disconnected"));
        return;
    }
    std::array<char, 4096> input{};
    // A bounded receive budget keeps rendering and input responsive under heavy output.
    for (int i = 0; i < 4; ++i)
    {
        auto n = ::recv(fd_, input.data(), input.size(), 0);
        if (n > 0)
        {
            model.feed(telnet_.feed(input.data(), static_cast<std::size_t>(n)));
            if (!queue(telnet_.reply()))
            {
                fail(screen, N_("Protocol buffer full"));
                return;
            }
        }
        else if (n == 0)
        {
            fail(screen, N_("Shell session ended"));
            return;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return;
        else
        {
            fail(screen, N_("Shell receive failed"));
            return;
        }
    }
}
#endif
} // namespace term
