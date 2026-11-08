// PS5Term - host side of the Linux guest agent protocol. GPL-3.0-or-later.
// Wire contract: docs/superpowers/specs/2026-11-03-linux-ssh-agent.md
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace term::platform
{
// Independent PTYs inside the guest, multiplexed over one byte stream. One owner
// thread calls everything. Bounded queues; nothing blocks; bytes are never lost.
class GuestAgent
{
  public:
    struct Transport
    {
        void *context = nullptr;
        std::size_t (*write)(void *, const void *, std::size_t) = nullptr;
        std::size_t (*read)(void *, void *, std::size_t) = nullptr;
    };
    enum class State
    {
        opening,
        running,
        exited,
        gone
    };
    static constexpr std::size_t max_sessions = 4, frame_payload = 4096, output_window = 8192;
    // Longest command (SSH clients such as Warp send several KiB); EXEC frames only.
    static constexpr std::size_t max_command = 16384, exec_payload = 5 + 32 + max_command;
    static constexpr std::size_t send_queue = 49152; // Room for a long command plus traffic.
    void attach(Transport) noexcept;
    void detach() noexcept; // Every session becomes gone.
    void pump() noexcept;
    // The agent has announced itself. Nothing may be sent before this: the device is
    // an ordinary echoing terminal until the agent switches it to raw.
    bool alive() const noexcept
    {
        return alive_ && !failed_;
    }
    bool failed() const noexcept
    {
        return failed_;
    }
    // Why the link failed, for the user and diagnostics; null while healthy.
    const char *failure() const noexcept
    {
        return failed_ ? (failure_ ? failure_ : "protocol error") : nullptr;
    }
    // Returns a handle above zero, or zero when all sessions are in use. An empty
    // command opens a login shell. A command runs with sh -c on exact bytes with a
    // real end of input, or on a PTY when `terminal` (the SSH client asked for one).
    int open(unsigned columns, unsigned rows, std::string_view term, std::string_view command = {},
             bool terminal = false) noexcept;
    std::size_t write(int handle, std::string_view bytes) noexcept; // Accepted prefix.
    std::size_t read(int handle, char *bytes, std::size_t capacity) noexcept;
    bool resize(int handle, unsigned columns, unsigned rows) noexcept;
    void finish_input(int handle) noexcept; // A command sees end of file after queued input.
    void close(int handle) noexcept;
    State state(int handle, int *status = nullptr) const noexcept;
    std::size_t open_sessions() const noexcept;
    std::size_t queued_bytes() const noexcept
    {
        return tx_size_;
    }

  private:
    struct Session
    {
        std::uint32_t id = 0;
        State state = State::gone;
        int status = 0;
        bool input_inflight = false, open_sent = false;
        std::array<char, frame_payload> input{};
        std::size_t input_size = 0, input_allowance = 0;
        std::array<char, output_window> output{};
        std::size_t output_head = 0, output_size = 0, credit_due = 0;
        unsigned columns = 80, rows = 24;
        bool resize_due = false, eof_due = false;
    };
    Transport transport_{};
    std::array<Session, max_sessions> sessions_{};
    // Close frames that did not fit in tx_; the guest shell runs until one is sent.
    std::array<std::uint32_t, max_sessions> close_due_{};
    std::array<unsigned char, send_queue> tx_{};
    std::size_t tx_size_ = 0;
    std::array<unsigned char, 16 + frame_payload> rx_{};
    std::size_t rx_size_ = 0;
    std::uint32_t last_id_ = 0;
    bool alive_ = false, failed_ = false;
    const char *failure_ = nullptr;
    bool enqueue(unsigned type, std::uint32_t id, const void *payload, std::size_t size) noexcept;
    void fail(const char *reason) noexcept;
    void handle_frame() noexcept;
    void pump_session(Session &) noexcept;
    Session *find(std::uint32_t id) noexcept;
    const Session *by_handle(int handle) const noexcept;
    Session *by_handle(int handle) noexcept;
};
} // namespace term::platform
