// PS5Term - run a PS5 program for a tab through the ps5term-run payload. GPL-3.0-or-later.
// The app cannot create processes, so it writes a request, sends the launcher payload to the
// console's ELF loader and accepts the launcher's two loopback connections: data (the
// program's stdin/stdout/stderr) and control. See
// docs/superpowers/specs/2026-11-08-ps5-programs.md.
#pragma once
#include "launcher.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace term::platform
{
class ProgramRunner
{
  public:
    struct Config
    {
        const char *request_path = "/data/ps5term/run/request";
#if defined(__PROSPERO__)
        const char *launcher_path = "/app0/assets/backend/ps5term-run.elf";
#else
        const char *launcher_path = "assets/backend/ps5term-run.elf";
#endif
        // xterm-256color's terminfo entry, copied once to where the launcher points TERMINFO
        // (the PS5 has no terminfo database; ncurses needs one).
#if defined(__PROSPERO__)
        const char *terminfo_source = "/app0/assets/terminfo/x/xterm-256color";
#else
        const char *terminfo_source = "assets/terminfo/x/xterm-256color";
#endif
        const char *terminfo_directory = "/data/ps5term/terminfo";
        unsigned loader_port = 9021;
        std::uint64_t connect_timeout_ms = 10000;
        // After the launcher has connected: reading a large program from slow storage and
        // loading it can take much longer than the connection itself.
        std::uint64_t start_timeout_ms = 120000;
    };
    enum class State
    {
        idle,
        sending,  // the launcher is going to the ELF loader
        waiting,  // for the launcher to connect back and start the program
        running,  // the program runs; data flows
        finished, // it ended: exited() or killed() says how
        failed    // it never started: failure() says why
    };
    static constexpr std::size_t max_arguments = 32, max_text = 6144;

    ProgramRunner() noexcept : ProgramRunner(Config{})
    {
    }
    explicit ProgramRunner(Config config) noexcept : config_(config)
    {
    }
    ~ProgramRunner();
    void configure(Config config) noexcept // Before start(); tests use their own paths.
    {
        config_ = config;
    }
    ProgramRunner(const ProgramRunner &) = delete;
    ProgramRunner &operator=(const ProgramRunner &) = delete;

    // arguments: NUL-separated, the first is the program's absolute path.
    bool start(std::string_view arguments, std::string_view cwd, unsigned columns,
               unsigned rows) noexcept;
    void tick() noexcept;
    IoResult read(char *bytes, std::size_t size) noexcept;        // the program's output
    IoResult write(const char *bytes, std::size_t size) noexcept; // its input
    bool interrupt() noexcept;                                    // SIGINT
    bool cancel() noexcept; // Give up a launch that has not started the program yet.
    void stop() noexcept;   // Closes everything; the launcher then kills the program.
    State state() const noexcept
    {
        return state_;
    }
    // English, like the shell's own messages: why it never started.
    const char *failure() const noexcept
    {
        return failure_.data();
    }
    bool killed() const noexcept
    {
        return killed_;
    }
    int status() const noexcept // exit code, or the signal when killed()
    {
        return status_;
    }
    bool output_closed() const noexcept
    {
        return data_ < 0;
    }

  private:
    Config config_;
    State state_ = State::idle;
    int listener_ = -1, loader_ = -1, data_ = -1, control_ = -1;
    struct Pending
    {
        int fd = -1;
        std::array<char, 80> line{};
        std::size_t size = 0;
    };
    std::array<Pending, 4> pending_{};
    std::array<char, 33> token_{};
    unsigned char *launcher_ = nullptr;
    std::size_t launcher_size_ = 0, launcher_sent_ = 0;
    std::uint64_t deadline_ = 0;
    std::array<char, 160> control_line_{};
    std::size_t control_size_ = 0;
    std::array<char, 160> failure_{};
    bool killed_ = false;
    int status_ = 0;
    // One request file serves every tab, so only one launch may be between writing it and
    // its launcher connecting back.
    static ProgramRunner *launching_;
    void fail(const char *reason) noexcept;
    void close_all() noexcept;
    void accept_pending() noexcept;
    void read_control() noexcept;
    void control_message(std::string_view line) noexcept;
};
} // namespace term::platform
