// PS5Term - nonblocking local shell connection. GPL-3.0-or-later.
#pragma once
#include "terminal_model.hpp"
#include "i18n.hpp"
#include "terminal_transport.hpp"
#include "telnet.hpp"
#include "builtin_shell.hpp"
#include "platform/launcher.hpp"
#include "platform/pty_launcher.hpp"
#include "platform/linux_subsystem.hpp"
#include "platform/program_runner.hpp"
#include <array>
#include <cstdint>
namespace term
{
class Connection
{
  public:
    // A supplied launcher is borrowed; its owner must outlive the connection.
    explicit Connection(platform::LauncherApi api = {}) noexcept
        : pty_api_(api), borrowed_launcher_(api.context != nullptr)
    {
        builtin_.enable_programs(true); // `run` starts PS5 programs in this tab.
    }
    // Independent PTY borrowed from the app's single Linux VM. Owner outlives us.
    // A command (a static string) runs on the tab's PTY instead of a login shell.
    explicit Connection(platform::GuestAgent *peer, const char *command = nullptr) noexcept
        : guest_peer_(peer), guest_command_(command)
    {
        environment_ = Environment::linux_guest;
    }
    void tick(TerminalModel &model, std::uint64_t now) noexcept;
    bool submit(std::string_view command) noexcept;
    bool write_raw(const char *data, std::size_t size) noexcept;
    bool interrupt() noexcept;
    void reconnect() noexcept;
    enum class Environment
    {
        console,
        linux_guest
    };
    bool select_environment(Environment environment) noexcept;
    Environment environment() const noexcept
    {
        return environment_;
    }
    static constexpr bool linux_available() noexcept
    {
        return platform::LinuxSubsystem::available();
    }
    bool linux_mounts_whole_console() const noexcept
    {
        return linux_vm_.mounts_whole_console();
    }
    bool set_linux_mounts_whole_console(bool whole) noexcept
    {
        return linux_vm_.set_mounts_whole_console(whole);
    }
    bool expand_linux_storage(unsigned mib) noexcept
    {
        return linux_vm_.expand_storage(mib);
    }
    bool storage_busy() const noexcept
    {
        return linux_vm_.storage_busy();
    }
    unsigned storage_progress() const noexcept
    {
        return linux_vm_.storage_progress();
    }
    int storage_error() const noexcept
    {
        return linux_vm_.storage_error();
    }
    // The Linux data disk's size in MiB; 0 when there is none yet.
    unsigned storage_mib() const noexcept
    {
        return linux_vm_.storage_mib();
    }
    bool connected() const noexcept
    {
        return state_ == State::ready;
    }
    bool failed() const noexcept
    {
        return state_ == State::failed;
    }
    std::string_view status() const noexcept
    {
        return linux_ && !guest_peer_ && !failed() ? linux_vm_.status() : status_;
    }
    // Keys go straight to the program (no app line editor): a native or Linux shell, or a
    // PS5 program from `run` once it has switched to a full screen, as ncurses does.
    bool native() const noexcept
    {
        return native_ || linux_ || (program_mode_ && program_raw_);
    }
    bool running_program() const noexcept
    {
        return program_mode_;
    }
    void configure_programs(platform::ProgramRunner::Config config) noexcept
    {
        program_.configure(config);
    }
    bool resize(unsigned cols, unsigned rows) noexcept;
    platform::GuestAgent &guest_agent() noexcept
    {
        return linux_vm_.agent();
    }
    ~Connection();

  private:
    platform::GuestAgent *guest_peer_ = nullptr;
    const char *guest_command_ = nullptr;
    int guest_handle_ = 0;
    BuiltinShell builtin_;
    platform::ProgramRunner program_;
    bool program_mode_ = false, program_raw_ = false;
    std::uint64_t program_ended_at_ = 0;
    void start_program(TerminalModel &model) noexcept;
    void tick_program(TerminalModel &model, std::uint64_t now) noexcept;
    void finish_program(TerminalModel &model) noexcept;
    platform::LauncherApi pty_api_{};
    platform::PlatformLauncher pty_launcher_;
    platform::ChildId pty_child_{};
    platform::LinuxSubsystem linux_vm_;
    Environment environment_ = Environment::console;
    bool native_ = false;
    bool linux_ = false;
    bool borrowed_launcher_ = false;
    TerminalTransport transport_;
    static platform::IoResult transport_write(void *, const char *, std::size_t) noexcept;
    static platform::IoResult transport_read(void *, char *, std::size_t) noexcept;
    enum class State
    {
        idle,
        connecting,
        ready,
        loader_connect,
        uploading,
        waiting,
        failed
    };
    State state_ = State::idle;
    int fd_ = -1, file_ = -1;
    bool launched_ = false;
    std::uint64_t deadline_ = 0, next_ = 0;
    std::string_view status_ = N_("Starting local shell");
    Telnet telnet_;
    std::array<char, 8192> outgoing_{};
    std::size_t queued_ = 0, sent_ = 0;
    int connect_port(unsigned port) noexcept;
    bool queue(std::string_view bytes) noexcept;
    bool flush() noexcept;
    void close_socket() noexcept;
    void fail(TerminalModel &model, std::string_view reason) noexcept;
    bool try_native(TerminalModel &model) noexcept;
    void tick_native(TerminalModel &model) noexcept;
    bool try_linux(TerminalModel &model) noexcept;
    void tick_linux(TerminalModel &model) noexcept;
};
} // namespace term
