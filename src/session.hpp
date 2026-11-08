// PS5Term - bounded session ownership and I/O. GPL-3.0-or-later.
#pragma once
#include "platform/launcher.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace term
{
inline constexpr std::size_t session_limit = 4, session_queue_size = 256 * 1024;
inline constexpr std::size_t session_io_chunk = 16 * 1024, session_read_budget = 64 * 1024;
struct SessionId
{
    std::size_t slot = session_limit;
    std::uint64_t generation = 0;
    bool valid() const noexcept
    {
        return slot < session_limit && generation;
    }
};
enum class SessionResult
{
    ok,
    stale,
    invalid,
    backpressure,
    unsupported,
    failed
};
enum class SessionPhase
{
    empty,
    running,
    exited,
    lost,
    failed,
    expired
};
struct SessionStatus
{
    SessionPhase phase = SessionPhase::empty;
    int exit_status = 0, native_error = 0;
};
class SessionManager
{
  public:
    explicit SessionManager(platform::PlatformLauncher &launcher) noexcept : launcher_(launcher)
    {
    }
    ~SessionManager();
    SessionManager(const SessionManager &) = delete;
    SessionManager &operator=(const SessionManager &) = delete;
    SessionId open(const char *program, std::uint64_t now) noexcept;
    SessionResult write(SessionId, const char *, std::size_t) noexcept;
    std::size_t read(SessionId, char *, std::size_t) noexcept;
    SessionResult resize(SessionId, unsigned cols, unsigned rows) noexcept;
    SessionResult signal(SessionId, platform::ChildSignal) noexcept;
    SessionResult close(SessionId) noexcept;
    bool renew(SessionId, std::uint64_t now) noexcept;
    void poll(std::uint64_t now) noexcept;
    SessionStatus status(SessionId) const noexcept;
    std::size_t pending_write(SessionId) const noexcept;

  private:
    struct Queue
    {
        std::array<char, session_queue_size> bytes{};
        std::size_t head = 0, size = 0;
        std::size_t space() const noexcept
        {
            return bytes.size() - size;
        }
        void push(const char *, std::size_t) noexcept;
        std::size_t peek(char *, std::size_t) const noexcept;
        void drop(std::size_t) noexcept;
        void clear() noexcept;
    };
    struct Slot
    {
        std::uint64_t generation = 0, heartbeat = 0;
        platform::ChildId child{};
        SessionStatus status{};
        Queue input{}, output{};
    };
    platform::PlatformLauncher &launcher_;
    std::array<Slot, session_limit> slots_{};
    std::uint64_t generation_ = 0;
    Slot *find(SessionId) noexcept;
    const Slot *find(SessionId) const noexcept;
    void stop(Slot &, SessionPhase, int error = 0) noexcept;
};
} // namespace term
