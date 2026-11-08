// PS5Term - GPL-3.0-or-later.
#include "guest_agent.hpp"
#include <algorithm>
#include <cstring>
namespace term::platform
{
namespace
{
enum : unsigned
{
    open_frame = 1,
    opened_frame,
    input_frame,
    input_ok_frame,
    output_frame,
    credit_frame,
    resize_frame,
    close_frame,
    exit_frame,
    hello_frame,
    exec_frame,
    input_eof_frame
};
constexpr std::uint32_t control_id = 0xffffffffU;
constexpr std::size_t header_size = 16;
std::uint32_t read32(const unsigned char *p) noexcept
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
           p[3];
}
void put32(unsigned char *p, std::uint32_t value) noexcept
{
    p[0] = static_cast<unsigned char>(value >> 24);
    p[1] = static_cast<unsigned char>(value >> 16);
    p[2] = static_cast<unsigned char>(value >> 8);
    p[3] = static_cast<unsigned char>(value);
}
void put16(unsigned char *p, unsigned value) noexcept
{
    p[0] = static_cast<unsigned char>(value >> 8);
    p[1] = static_cast<unsigned char>(value);
}
// The guest carries terminfo for these. Other clients are close to xterm.
std::string_view usable_term(std::string_view term) noexcept
{
    for (std::string_view known : {"xterm", "xterm-256color", "screen", "screen-256color", "tmux",
                                   "tmux-256color", "vt100", "dumb"})
        if (term == known)
            return term;
    return "xterm-256color";
}
unsigned clamp_size(unsigned value, unsigned limit, unsigned fallback) noexcept
{
    return value ? std::min(value, limit) : fallback;
}
} // namespace
void GuestAgent::attach(Transport transport) noexcept
{
    detach();
    transport_ = transport;
}
void GuestAgent::detach() noexcept
{
    transport_ = {};
    for (auto &session : sessions_)
        if (session.id)
            session.state = State::gone;
    tx_size_ = rx_size_ = 0;
    alive_ = failed_ = false;
    failure_ = nullptr;
    close_due_.fill(0);
}
void GuestAgent::fail(const char *reason) noexcept
{
    failure_ = reason;
    failed_ = true;
    for (auto &session : sessions_)
        if (session.id && session.state != State::exited)
            session.state = State::gone;
}
GuestAgent::Session *GuestAgent::find(std::uint32_t id) noexcept
{
    for (auto &session : sessions_)
        if (session.id == id)
            return &session;
    return nullptr;
}
const GuestAgent::Session *GuestAgent::by_handle(int handle) const noexcept
{
    if (handle <= 0)
        return nullptr;
    for (const auto &session : sessions_)
        if (session.id == static_cast<std::uint32_t>(handle))
            return &session;
    return nullptr;
}
GuestAgent::Session *GuestAgent::by_handle(int handle) noexcept
{
    return const_cast<Session *>(static_cast<const GuestAgent *>(this)->by_handle(handle));
}
bool GuestAgent::enqueue(unsigned type, std::uint32_t id, const void *payload,
                         std::size_t size) noexcept
{
    if (size > (type == exec_frame ? exec_payload : frame_payload) ||
        header_size + size > tx_.size() - tx_size_)
        return false;
    auto *p = tx_.data() + tx_size_;
    std::memcpy(p, "P5GA", 4);
    p[4] = 1;
    p[5] = static_cast<unsigned char>(type);
    p[6] = p[7] = 0;
    put32(p + 8, id);
    put32(p + 12, static_cast<std::uint32_t>(size));
    if (size)
        std::memcpy(p + header_size, payload, size);
    tx_size_ += header_size + size;
    return true;
}
int GuestAgent::open(unsigned columns, unsigned rows, std::string_view term,
                     std::string_view command, bool terminal) noexcept
{
    if (!alive() || !transport_.write || command.size() > max_command ||
        command.find('\0') != command.npos)
        return 0;
    // Slots hold an exited session until its owner closes it.
    Session *slot = nullptr;
    for (auto &session : sessions_)
        if (!session.id)
        {
            slot = &session;
            break;
        }
    // A shell whose close is still queued holds a guest slot too.
    const auto closing = static_cast<std::size_t>(
        std::count_if(close_due_.begin(), close_due_.end(), [](std::uint32_t id) { return id; }));
    if (!slot || last_id_ >= 0x7ffffffeU || open_sessions() + closing >= max_sessions)
        return 0;
    const auto name = usable_term(term);
    // One owner thread calls this; keep the 16 KiB frame off the stack.
    static std::array<unsigned char, exec_payload> payload;
    columns = clamp_size(columns, 500, 80);
    rows = clamp_size(rows, 200, 24);
    put16(payload.data(), columns);
    put16(payload.data() + 2, rows);
    std::size_t size = 4;
    if (command.empty())
    {
        std::memcpy(payload.data() + size, name.data(), name.size());
        size += name.size();
    }
    else
    {
        // The high bit asks the guest for a PTY instead of a socket pair.
        payload[size++] = static_cast<unsigned char>(name.size() | (terminal ? 0x80U : 0U));
        std::memcpy(payload.data() + size, name.data(), name.size());
        size += name.size();
        std::memcpy(payload.data() + size, command.data(), command.size());
        size += command.size();
    }
    const auto id = last_id_ + 1;
    if (!enqueue(command.empty() ? open_frame : exec_frame, id, payload.data(), size))
        return 0;
    last_id_ = id;
    *slot = Session{};
    slot->id = id;
    slot->state = State::opening;
    slot->columns = columns;
    slot->rows = rows;
    return static_cast<int>(id);
}
std::size_t GuestAgent::write(int handle, std::string_view bytes) noexcept
{
    auto *session = by_handle(handle);
    if (!session || session->state == State::gone || session->state == State::exited)
        return bytes.size(); // Nothing will read it; do not stall the sender.
    const auto count = std::min(bytes.size(), session->input.size() - session->input_size);
    std::memcpy(session->input.data() + session->input_size, bytes.data(), count);
    session->input_size += count;
    return count;
}
std::size_t GuestAgent::read(int handle, char *bytes, std::size_t capacity) noexcept
{
    auto *session = by_handle(handle);
    if (!session || !bytes)
        return 0;
    const auto count = std::min(capacity, session->output_size);
    for (std::size_t i = 0; i < count; ++i)
        bytes[i] = session->output[(session->output_head + i) % output_window];
    session->output_head = (session->output_head + count) % output_window;
    session->output_size -= count;
    session->credit_due += count; // Only freed space is offered back to the guest.
    return count;
}
bool GuestAgent::resize(int handle, unsigned columns, unsigned rows) noexcept
{
    auto *session = by_handle(handle);
    if (!session || session->state == State::gone || session->state == State::exited)
        return false;
    session->columns = clamp_size(columns, 500, session->columns);
    session->rows = clamp_size(rows, 200, session->rows);
    session->resize_due = true;
    return true;
}
void GuestAgent::finish_input(int handle) noexcept
{
    if (auto *session = by_handle(handle))
        session->eof_due = true;
}
void GuestAgent::close(int handle) noexcept
{
    auto *session = by_handle(handle);
    if (!session)
        return;
    // Late frames for a closed ID are recognised by last_id_ and discarded.
    if ((session->state == State::running || session->state == State::opening) &&
        !enqueue(close_frame, session->id, nullptr, 0))
    {
        // Retry from pump(); dropping it would leave the guest shell running unowned.
        auto free_entry = std::find(close_due_.begin(), close_due_.end(), 0U);
        if (free_entry == close_due_.end())
            fail("close queue full");
        else
            *free_entry = session->id;
    }
    *session = Session{};
}
GuestAgent::State GuestAgent::state(int handle, int *status) const noexcept
{
    const auto *session = by_handle(handle);
    if (!session)
        return State::gone;
    if (status)
        *status = session->status;
    // Exited only once all output has been read.
    if (session->state == State::exited && session->output_size)
        return State::running;
    return session->state;
}
std::size_t GuestAgent::open_sessions() const noexcept
{
    return static_cast<std::size_t>(std::count_if(sessions_.begin(), sessions_.end(),
                                                  [](const Session &s)
                                                  { return s.id && s.state != State::gone; }));
}
void GuestAgent::handle_frame() noexcept
{
    const unsigned type = rx_[5];
    const auto id = read32(rx_.data() + 8);
    const auto size = static_cast<std::size_t>(read32(rx_.data() + 12));
    const auto *payload = rx_.data() + header_size;
    if (id == control_id)
    {
        if (type != hello_frame || size)
        {
            fail("bad control frame");
            return;
        }
        // A second announcement means the agent restarted and its shells are gone.
        if (alive_)
        {
            for (auto &session : sessions_)
                if (session.id && session.state != State::exited)
                    session.state = State::gone;
            close_due_.fill(0);
        }
        alive_ = true;
        return;
    }
    auto *session = find(id);
    if (!session)
    {
        // Traffic for a closed session crossing in flight; an unknown future ID is corrupt.
        if (id > last_id_ || !id)
            fail("frame for unknown session");
        return;
    }
    switch (type)
    {
    case opened_frame:
        if (size != 4 || session->state != State::opening)
        {
            fail("unexpected opened");
            return;
        }
        session->state = State::running;
        session->input_allowance = read32(payload);
        session->credit_due = output_window;
        return;
    case input_ok_frame:
        if (size != 4 || !session->input_inflight)
        {
            fail("unexpected input acknowledgement");
            return;
        }
        session->input_inflight = false;
        return;
    case output_frame:
        if (size > output_window - session->output_size)
        {
            fail("output beyond credit");
            return;
        }
        for (std::size_t i = 0; i < size; ++i)
            session->output[(session->output_head + session->output_size + i) % output_window] =
                static_cast<char>(payload[i]);
        session->output_size += size;
        return;
    case exit_frame:
        if (size != 4)
        {
            fail("bad exit frame");
            return;
        }
        session->status = static_cast<int>(read32(payload));
        session->state = State::exited;
        return;
    default:
        fail("unknown frame type");
        return;
    }
}
void GuestAgent::pump_session(Session &session) noexcept
{
    if (session.state != State::running)
        return;
    if (session.input_size && !session.input_inflight)
    {
        const auto count =
            std::min({session.input_size, frame_payload,
                      session.input_allowance ? session.input_allowance : frame_payload});
        if (enqueue(input_frame, session.id, session.input.data(), count))
        {
            session.input_inflight = true;
            session.input_size -= count;
            std::memmove(session.input.data(), session.input.data() + count, session.input_size);
        }
    }
    if (session.eof_due && !session.input_size && !session.input_inflight &&
        enqueue(input_eof_frame, session.id, nullptr, 0))
        session.eof_due = false;
    if (session.credit_due)
    {
        unsigned char value[4];
        put32(value, static_cast<std::uint32_t>(session.credit_due));
        if (enqueue(credit_frame, session.id, value, 4))
            session.credit_due = 0;
    }
    if (session.resize_due)
    {
        unsigned char value[4];
        put16(value, session.columns);
        put16(value + 2, session.rows);
        if (enqueue(resize_frame, session.id, value, 4))
            session.resize_due = false;
    }
}
void GuestAgent::pump() noexcept
{
    if (!transport_.write || failed_)
        return;
    for (auto &id : close_due_)
        if (id && enqueue(close_frame, id, nullptr, 0))
            id = 0;
    for (auto &session : sessions_)
        if (session.id)
            pump_session(session);
    if (tx_size_)
    {
        const auto sent = transport_.write(transport_.context, tx_.data(), tx_size_);
        tx_size_ -= sent;
        std::memmove(tx_.data(), tx_.data() + sent, tx_size_);
    }
    // Enough for every session's window per call without unbounded work in one frame.
    for (unsigned frames = 0; frames < 16 && !failed_; ++frames)
    {
        const std::size_t want =
            rx_size_ < header_size ? header_size : header_size + read32(rx_.data() + 12);
        if (want > rx_.size())
        {
            fail("oversized frame");
            return;
        }
        if (rx_size_ < want)
        {
            const auto got =
                transport_.read(transport_.context, rx_.data() + rx_size_, want - rx_size_);
            rx_size_ += got;
            if (rx_size_ < want)
                return;
            if (want == header_size)
            {
                if (std::memcmp(rx_.data(), "P5GA", 4) || rx_[4] != 1 || rx_[6] || rx_[7] ||
                    read32(rx_.data() + 12) > frame_payload)
                {
                    fail("corrupt frame header");
                    return;
                }
                if (read32(rx_.data() + 12))
                    continue;
            }
        }
        handle_frame();
        rx_size_ = 0;
    }
}
} // namespace term::platform
