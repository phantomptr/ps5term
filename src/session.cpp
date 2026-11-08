// PS5Term - GPL-3.0-or-later.
#include "session.hpp"
#include <algorithm>
#include <cerrno>
namespace term
{
void SessionManager::Queue::push(const char *data, std::size_t count) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
        bytes[(head + size + i) % bytes.size()] = data[i];
    size += count;
}
std::size_t SessionManager::Queue::peek(char *data, std::size_t count) const noexcept
{
    count = std::min(count, size);
    for (std::size_t i = 0; i < count; ++i)
        data[i] = bytes[(head + i) % bytes.size()];
    return count;
}
void SessionManager::Queue::drop(std::size_t count) noexcept
{
    count = std::min(count, size);
    for (std::size_t i = 0; i < count; ++i)
        bytes[(head + i) % bytes.size()] = 0;
    head = (head + count) % bytes.size();
    size -= count;
}
void SessionManager::Queue::clear() noexcept
{
    auto *out = static_cast<volatile char *>(bytes.data());
    for (std::size_t i = 0; i < bytes.size(); ++i)
        out[i] = 0;
    head = size = 0;
}
SessionManager::~SessionManager()
{
    for (auto &slot : slots_)
    {
        stop(slot, SessionPhase::empty);
        slot.input.clear();
        slot.output.clear();
    }
}
SessionManager::Slot *SessionManager::find(SessionId id) noexcept
{
    return id.valid() && slots_[id.slot].generation == id.generation ? &slots_[id.slot] : nullptr;
}
const SessionManager::Slot *SessionManager::find(SessionId id) const noexcept
{
    return id.valid() && slots_[id.slot].generation == id.generation ? &slots_[id.slot] : nullptr;
}
void SessionManager::stop(Slot &slot, SessionPhase phase, int error) noexcept
{
    if (slot.child.valid())
    {
        const auto result = launcher_.stop(slot.child);
        if (!result.stopped)
        {
            slot.status = {SessionPhase::failed, 0, result.native_error};
            return;
        }
        slot.child = {};
    }
    slot.status = {phase, 0, error};
    slot.input.clear();
}
SessionId SessionManager::open(const char *program, std::uint64_t now) noexcept
{
    if (!program || !*program || generation_ == UINT64_MAX)
        return {};
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        auto &slot = slots_[i];
        // Output belongs to the old identity until consumed or explicitly closed.
        if (slot.child.valid() || slot.output.size)
            continue;
        const auto generation = ++generation_;
        auto result = launcher_.start({program, generation});
        if (!result.child.valid())
            return {};
        // Adapter must return this exact generation; otherwise it still owns cleanup.
        if (result.child.generation != generation)
        {
            launcher_.stop(result.child);
            return {};
        }
        slot.generation = generation;
        slot.heartbeat = now;
        slot.child = result.child;
        slot.status = {SessionPhase::running, 0, 0};
        slot.input.clear();
        slot.output.clear();
        return {i, generation};
    }
    return {};
}
SessionResult SessionManager::write(SessionId id, const char *data, std::size_t size) noexcept
{
    auto *slot = find(id);
    if (!slot || slot->status.phase != SessionPhase::running)
        return SessionResult::stale;
    if (!data && size)
        return SessionResult::invalid;
    if (size > slot->input.space())
        return SessionResult::backpressure;
    slot->input.push(data, size);
    return SessionResult::ok;
}
std::size_t SessionManager::read(SessionId id, char *data, std::size_t size) noexcept
{
    auto *slot = find(id);
    if (!slot || !data)
        return 0;
    auto count = slot->output.peek(data, size);
    slot->output.drop(count);
    return count;
}
std::size_t SessionManager::pending_write(SessionId id) const noexcept
{
    auto *slot = find(id);
    return slot ? slot->input.size : 0;
}
SessionStatus SessionManager::status(SessionId id) const noexcept
{
    auto *slot = find(id);
    return slot ? slot->status : SessionStatus{SessionPhase::lost, 0, ESTALE};
}
SessionResult SessionManager::close(SessionId id) noexcept
{
    auto *slot = find(id);
    if (!slot || slot->status.phase == SessionPhase::empty)
        return SessionResult::stale;
    stop(*slot, SessionPhase::empty);
    slot->output.clear();
    return slot->child.valid() ? SessionResult::failed : SessionResult::ok;
}
SessionResult SessionManager::resize(SessionId id, unsigned cols, unsigned rows) noexcept
{
    auto *slot = find(id);
    if (!slot || !slot->child.valid())
        return SessionResult::stale;
    if (!cols || !rows || cols > 4096 || rows > 4096)
        return SessionResult::invalid;
    return launcher_.resize(slot->child, cols, rows) ? SessionResult::ok
                                                     : SessionResult::unsupported;
}
SessionResult SessionManager::signal(SessionId id, platform::ChildSignal signal) noexcept
{
    auto *slot = find(id);
    if (!slot || !slot->child.valid())
        return SessionResult::stale;
    return launcher_.signal(slot->child, signal) ? SessionResult::ok : SessionResult::unsupported;
}
bool SessionManager::renew(SessionId id, std::uint64_t now) noexcept
{
    auto *slot = find(id);
    if (!slot || !slot->child.valid() || now < slot->heartbeat || now - slot->heartbeat >= 10000000)
        return false;
    slot->heartbeat = now;
    return true;
}
void SessionManager::poll(std::uint64_t now) noexcept
{
    std::array<char, session_io_chunk> bytes{};
    for (auto &slot : slots_)
    {
        if (!slot.child.valid())
            continue;
        if (now < slot.heartbeat || now - slot.heartbeat >= 10000000)
        {
            stop(slot, SessionPhase::expired, ETIMEDOUT);
            continue;
        }
        const auto child = launcher_.poll(slot.child);
        if (child.phase == platform::ChildPhase::lost)
        {
            slot.child = {};
            slot.status = {SessionPhase::lost, 0, ESTALE};
            slot.input.clear();
            continue;
        }
        if (child.phase == platform::ChildPhase::failed ||
            child.phase == platform::ChildPhase::unknown)
        {
            stop(slot, SessionPhase::failed, child.native_error);
            continue;
        }
        // One input write and <=64 KiB output per session/tick (<=256 KiB globally).
        if (child.phase == platform::ChildPhase::running && slot.input.size)
        {
            auto size = slot.input.peek(bytes.data(), bytes.size());
            auto result = launcher_.write(slot.child, bytes.data(), size);
            if (result.count > size || result.state == platform::IoState::failed ||
                result.state == platform::IoState::eof)
            {
                stop(slot, SessionPhase::failed, result.native_error ? result.native_error : EIO);
                continue;
            }
            slot.input.drop(result.count);
        }
        bool read_error = false, read_eof = false;
        for (std::size_t budget = 0; budget < session_read_budget && slot.output.space();)
        {
            const auto count =
                std::min({bytes.size(), slot.output.space(), session_read_budget - budget});
            auto result = launcher_.read(slot.child, bytes.data(), count);
            if (result.count > count || result.state == platform::IoState::failed)
            {
                read_error = true;
                stop(slot, SessionPhase::failed, result.native_error ? result.native_error : EIO);
                break;
            }
            slot.output.push(bytes.data(), result.count);
            budget += result.count;
            read_eof = result.state == platform::IoState::eof;
            if (!result.count || result.state != platform::IoState::ok)
                break;
        }
        if (!read_error && child.phase == platform::ChildPhase::exited)
        {
            // Retain the adapter handle until EOF, including across full queues
            // and read budgets. Reaping a child must not discard buffered output.
            slot.status = {SessionPhase::exited, child.exit_status, child.native_error};
            if (read_eof)
                slot.child = {};
            slot.input.clear();
        }
    }
}
} // namespace term
