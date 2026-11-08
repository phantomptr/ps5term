// PS5Term - GPL-3.0-or-later. Uses wolfSSH public API only.
#include "connection.hpp"
#include <wolfssh/error.h>
#if defined(WOLFSSH_SFTP)
#include <wolfssh/wolfsftp.h>
#endif
#include <algorithm>
#include <cstring>
#include <new>

namespace term::ssh
{
Connection::Connection(const ServerConfig &config, TransportIO io) noexcept
    : config_(config), io_(io)
{
    error_ = WS_BAD_ARGUMENT;
    if (config.username.empty() || config.password.empty() || config.password.size() > 128 ||
        config.host_key_der.empty() || config.host_key_der.size() > 65536 || !io.receive ||
        !io.send)
        return;
    context_ = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, nullptr);
    if (!context_)
    {
        error_ = WS_MEMORY_E;
        return;
    }
    wolfSSH_SetUserAuth(context_, auth);
    wolfSSH_SetUserAuthTypes(context_, auth_types);
    // An agent offering several keys must not use up the password attempts below.
    wolfSSH_CTX_SetMaxAuthAttempts(context_, 10);
    wolfSSH_SetIORecv(context_, receive);
    wolfSSH_SetIOSend(context_, send);
    wolfSSH_CTX_SetChannelOpenCb(context_, channel_open);
    wolfSSH_CTX_SetChannelReqAnyCb(context_, request);
    error_ = wolfSSH_CTX_SetSshProtoIdStr(context_, "SSH-2.0-PS5Term_owned_host\r\n");
    if (error_ != WS_SUCCESS)
        return;
    error_ = wolfSSH_CTX_UsePrivateKey_buffer(context_, config.host_key_der.data(),
                                              static_cast<word32>(config.host_key_der.size()),
                                              WOLFSSH_FORMAT_ASN1);
    if (error_ != WS_SUCCESS)
        return;
    ssh_ = wolfSSH_new(context_);
    if (!ssh_)
    {
        error_ = WS_FATAL_ERROR;
        return;
    }
    wolfSSH_SetUserAuthCtx(ssh_, this);
    wolfSSH_SetChannelOpenCtx(ssh_, this);
    wolfSSH_SetChannelReqCtx(ssh_, this);
    wolfSSH_SetIOReadCtx(ssh_, this);
    wolfSSH_SetIOWriteCtx(ssh_, this);
}
Connection::~Connection()
{
    close();
}
void Connection::close() noexcept
{
    for (auto &session : sessions_)
    {
        if (session)
            release_session(*session);
        session.reset();
    }
    if (ssh_)
        wolfSSH_free(ssh_);
    ssh_ = nullptr;
    if (context_)
        wolfSSH_CTX_free(context_);
    context_ = nullptr;
}
void Connection::release_session(Session &session) noexcept
{
    session.command.reset();
    if (session.guest_handle > 0 && config_.guest)
        config_.guest->close(session.guest_handle);
    session.guest_handle = 0;
}
Connection::Session *Connection::find_session(WOLFSSH_CHANNEL *channel) noexcept
{
    word32 id = 0;
    if (!channel || wolfSSH_ChannelGetId(channel, &id, WS_CHANNEL_ID_SELF) != WS_SUCCESS)
        return nullptr;
    for (auto &session : sessions_)
        if (session && session->id == id)
            return session.get();
    return nullptr;
}
void Connection::reap_sessions() noexcept
{
    for (auto &session : sessions_)
        if (session && !wolfSSH_ChannelFind(ssh_, session->id, WS_CHANNEL_ID_SELF))
        {
            release_session(*session);
            session.reset();
        }
}
bool Connection::long_lived() const noexcept
{
    if (!accepted_)
        return false;
    for (const auto &session : sessions_)
        if (session && session->started && (session->has_pty || linux_login_))
            return true;
    return false;
}

int Connection::auth_types(WOLFSSH *, void *opaque)
{
    const auto *self = static_cast<const Connection *>(opaque);
    return WOLFSSH_USERAUTH_PASSWORD |
           (self && !self->config_.authorized_keys.empty() ? WOLFSSH_USERAUTH_PUBLICKEY : 0);
}
int Connection::auth(byte type, WS_UserAuthData *data, void *opaque)
{
    auto *self = static_cast<Connection *>(opaque);
    if (!self || !data || !data->username)
        return WOLFSSH_USERAUTH_FAILURE;
    // Same credentials either way: the name only selects where the session runs.
    const std::string_view login(reinterpret_cast<const char *>(data->username), data->usernameSz);
    const bool linux_login = login == "linux" && self->config_.guest;
    if (login != self->config_.username && !linux_login)
        return WOLFSSH_USERAUTH_FAILURE;
    self->linux_login_ = linux_login;
    if (type == WOLFSSH_USERAUTH_PUBLICKEY)
    {
        const auto &offered = data->sf.publicKey;
        if (!offered.publicKey || offered.isCert)
            return WOLFSSH_USERAUTH_FAILURE;
        for (const auto &key : self->config_.authorized_keys)
            if (key.size == offered.publicKeySz &&
                std::memcmp(key.bytes.data(), offered.publicKey, key.size) == 0)
                // wolfSSH independently verifies the signed request after this
                // allowlist check. A public-key probe alone never authenticates.
                return WOLFSSH_USERAUTH_SUCCESS;
        return WOLFSSH_USERAUTH_FAILURE;
    }
    if (type != WOLFSSH_USERAUTH_PASSWORD || !data->sf.password.password)
        return WOLFSSH_USERAUTH_FAILURE;
    const auto &password = data->sf.password;
    // Read only the configured length; compare every byte even on mismatch.
    volatile unsigned difference =
        static_cast<unsigned>(self->config_.password.size() ^ password.passwordSz);
    for (std::size_t i = 0; i < self->config_.password.size(); ++i)
        difference = difference | (static_cast<unsigned char>(self->config_.password[i]) ^
                                   (i < password.passwordSz ? password.password[i] : 0));
    if (difference == 0)
        return WOLFSSH_USERAUTH_SUCCESS;
    return ++self->password_failures_ >= 3 ? WOLFSSH_USERAUTH_REJECTED : WOLFSSH_USERAUTH_FAILURE;
}
int Connection::channel_open(WOLFSSH_CHANNEL *channel, void *opaque)
{
    auto *self = static_cast<Connection *>(opaque);
    const char *type = wolfSSH_ChannelGetType(channel);
    if (!self || !type || std::strcmp(type, "session") != 0)
        return WS_ERROR;
#if defined(WOLFSSH_SFTP)
    if (self->sftp_)
        return WS_ERROR;
#endif
    auto slot = std::find_if(self->sessions_.begin(), self->sessions_.end(),
                             [](const auto &s) { return !s; });
    if (slot == self->sessions_.end())
        return WS_ERROR;
    auto state = std::unique_ptr<Session>(new (std::nothrow) Session);
    if (!state || wolfSSH_ChannelGetId(channel, &state->id, WS_CHANNEL_ID_SELF) != WS_SUCCESS)
        return WS_ERROR;
    state->opened_at = self->now_;
    *slot = std::move(state);
    return WS_SUCCESS;
}

namespace
{
// Warp's SSH wrapper runs `ssh -t host '<POSIX shell bootstrap>'`. For a login shell it
// does not know, the bootstrap only starts that shell (`exec "$SHELL"`); the PS5 shell
// cannot run the script, so it starts its interactive shell directly instead.
bool warp_bootstrap(std::string_view command) noexcept
{
    while (!command.empty() && (command.front() == '\n' || command.front() == '\r'))
        command.remove_prefix(1);
    return command.starts_with("export TERM_PROGRAM='WarpTerminal'\n");
}
} // namespace
int Connection::request(WOLFSSH_CHANNEL *channel, const byte *type, word32 size, const byte *data,
                        word32 data_size, int, void *opaque)
{
    auto *self = static_cast<Connection *>(opaque);
    if (!self || !type)
        return WOLFSSH_REQ_REJECT;
    auto *state = self->find_session(channel);
    if (!state || state->closing)
        return WOLFSSH_REQ_REJECT;
    auto &session = *state;
    const std::string_view name(reinterpret_cast<const char *>(type), size);
    // PTY request: accept and let wolfSSH parse terminal modes. Extract initial
    // window size from the type-specific data (RFC 4254 §6.2): string TERM,
    // uint32 width-chars, uint32 height-rows, uint32 width-pixels, uint32
    // height-pixels, string terminal-modes.
    if (name == "pty-req" && !session.has_pty && data && data_size >= 21)
    {
        const word32 term_len =
            (word32(data[0]) << 24) | (word32(data[1]) << 16) | (word32(data[2]) << 8) | data[3];
        const word32 offset = 4 + term_len;
        if (term_len > 256 || offset + 16 > data_size)
            return WOLFSSH_REQ_REJECT;
        const auto kept = std::min<std::size_t>(term_len, session.pty_term.size() - 1);
        std::memcpy(session.pty_term.data(), data + 4, kept);
        session.pty_term[kept] = 0;
        session.pty_cols = (word32(data[offset]) << 24) | (word32(data[offset + 1]) << 16) |
                           (word32(data[offset + 2]) << 8) | data[offset + 3];
        session.pty_rows = (word32(data[offset + 4]) << 24) | (word32(data[offset + 5]) << 16) |
                           (word32(data[offset + 6]) << 8) | data[offset + 7];
        if (session.pty_cols == 0)
            session.pty_cols = 80;
        if (session.pty_rows == 0)
            session.pty_rows = 24;
        if (session.pty_cols > 500)
            session.pty_cols = 500;
        if (session.pty_rows > 200)
            session.pty_rows = 200;
        session.has_pty = true;
        return WOLFSSH_REQ_ACCEPT;
    }
    // Window-change (RFC 4254 §6.7): uint32 cols, uint32 rows, uint32 wpx, uint32 hpx.
    if (name == "window-change" && data && data_size >= 8)
    {
        auto cols =
            (word32(data[0]) << 24) | (word32(data[1]) << 16) | (word32(data[2]) << 8) | data[3];
        auto rows =
            (word32(data[4]) << 24) | (word32(data[5]) << 16) | (word32(data[6]) << 8) | data[7];
        if (cols > 0 && cols <= 500)
            session.pty_cols = cols;
        if (rows > 0 && rows <= 200)
            session.pty_rows = rows;
        if (session.guest_handle > 0)
            self->config_.guest->resize(session.guest_handle, session.pty_cols, session.pty_rows);
        return WOLFSSH_REQ_ACCEPT;
    }
    // Environment variables: accept and ignore. Clients send TERM, LANG, etc.
    if (name == "env")
        return WOLFSSH_REQ_ACCEPT;
    // Signal (RFC 4254 §6.9): accept SIGINT for Ctrl+C forwarding.
    if (name == "signal" && data && data_size >= 4)
    {
        const word32 sig_len =
            (word32(data[0]) << 24) | (word32(data[1]) << 16) | (word32(data[2]) << 8) | data[3];
        if (sig_len <= data_size - 4)
        {
            const std::string_view sig(reinterpret_cast<const char *>(data + 4), sig_len);
            if (sig == "INT" && session.command)
                session.command->interrupt();
            if (sig == "INT" && session.guest_handle > 0)
                self->config_.guest->write(session.guest_handle, {"\x03", 1});
        }
        return WOLFSSH_REQ_ACCEPT;
    }
    if (session.requested)
        return WOLFSSH_REQ_REJECT;
    if (name == "shell" && data_size == 0)
    {
        session.requested = true;
        return WOLFSSH_REQ_ACCEPT;
    }
    // Linux commands may be long (Warp's SSH bootstrap is several KiB); the native
    // shell keeps its short limit.
    const word32 command_limit =
        self->linux_login_ ||
                (session.has_pty && data_size > 4 &&
                 warp_bootstrap({reinterpret_cast<const char *>(data + 4), data_size - 4}))
            ? 16384
            : 512;
    if (name == "exec" && data && data_size >= 4 && data_size - 4 <= command_limit)
    {
        const word32 length =
            (word32(data[0]) << 24) | (word32(data[1]) << 16) | (word32(data[2]) << 8) | data[3];
        if (length != data_size - 4 || std::memchr(data + 4, 0, length))
            return WOLFSSH_REQ_REJECT;
        session.requested = true;
        return WOLFSSH_REQ_ACCEPT;
    }
#if defined(WOLFSSH_SFTP)
    if (!self->accepted_ &&
        std::count_if(self->sessions_.begin(), self->sessions_.end(),
                      [](const auto &s) { return bool(s); }) == 1 &&
        name == "subsystem" && data && data_size == 8 && data[0] == 0 && data[1] == 0 &&
        data[2] == 0 && data[3] == 4 && std::memcmp(data + 4, "sftp", 4) == 0)
    {
        self->sftp_ = true;
        session.requested = true;
        return WOLFSSH_REQ_ACCEPT;
    }
#endif
    return WOLFSSH_REQ_REJECT;
}
int Connection::receive(WOLFSSH *, void *buffer, word32 size, void *opaque)
{
    auto *self = static_cast<Connection *>(opaque);
    return self->io_.receive(self->io_.context, static_cast<unsigned char *>(buffer), size);
}
int Connection::send(WOLFSSH *, void *buffer, word32 size, void *opaque)
{
    auto *self = static_cast<Connection *>(opaque);
    return self->io_.send(self->io_.context, static_cast<const unsigned char *>(buffer), size);
}
bool Connection::tolerate(int status) const noexcept
{
    if (status == WS_FATAL_ERROR)
        status = wolfSSH_get_error(ssh_);
    return status == WS_SUCCESS || status == WS_WANT_READ || status == WS_WANT_WRITE ||
           status == WS_REKEYING || status == WS_CHAN_RXD || status == WS_EOF ||
           status == WS_CHANNEL_CLOSED;
}
bool Connection::send_pending(Session &session, WOLFSSH_CHANNEL *channel,
                              bool error_stream) noexcept
{
    auto &buffer = error_stream ? session.errors : session.output;
    auto &size = error_stream ? session.error_size : session.output_size;
    if (!size && session.command)
    {
        if (session.has_pty)
        {
            // A client that asked for a PTY put its terminal in raw mode, so this
            // line shell acts as the PTY: one stream, and bare LF becomes CRLF.
            if (error_stream)
                return true;
            char raw[2048];
            std::size_t count = session.command->read_stderr(raw, sizeof raw);
            if (!count)
                count = session.command->read_stdout(raw, sizeof raw);
            for (std::size_t i = 0; i < count; ++i)
            {
                if (raw[i] == '\n' && session.last_output != '\r')
                    buffer[size++] = '\r';
                buffer[size++] = raw[i];
                session.last_output = raw[i];
            }
        }
        else
            size = error_stream ? session.command->read_stderr(buffer.data(), buffer.size())
                                : session.command->read_stdout(buffer.data(), buffer.size());
    }
    if (!size)
        return true;
    auto *bytes = reinterpret_cast<byte *>(buffer.data());
    const int result = error_stream
                           ? wolfSSH_ChannelSendExt(channel, bytes, static_cast<word32>(size))
                           : wolfSSH_ChannelSend(channel, bytes, static_cast<word32>(size));
    if (result > 0)
    {
        if (static_cast<std::size_t>(result) > size)
            return false;
        size -= result;
        std::memmove(buffer.data(), buffer.data() + result, size);
        return true;
    }
    // A full SSH channel window is backpressure. Keep the exact unsent tail
    // while worker() receives the peer's window adjustment on later ticks.
    const int status = result == WS_FATAL_ERROR ? wolfSSH_get_error(ssh_) : result;
    if (status == WS_WANT_READ || status == WS_WANT_WRITE || status == WS_REKEYING ||
        status == WS_WINDOW_FULL || status == WS_SUCCESS)
        return true;
    error_ = status;
    return false;
}
void Connection::tick() noexcept
{
    if (!ssh_)
        return;
    if (draining_)
    {
        const int result = wolfSSH_shutdown(ssh_);
        if (!tolerate(result) || !wolfSSH_ChannelNext(ssh_, nullptr))
            close();
        return;
    }
    if (!accepted_)
    {
        const int result = wolfSSH_accept(ssh_);
#if defined(WOLFSSH_SFTP)
        if (result == WS_SFTP_COMPLETE)
        {
            std::array<char, 1024> path{};
            if (config_.directory.size() >= path.size())
            {
                close();
                return;
            }
            std::copy(config_.directory.begin(), config_.directory.end(), path.begin());
            if (wolfSSH_SFTP_SetDefaultPath(ssh_, path.data()) != WS_SUCCESS)
            {
                close();
                return;
            }
            sftp_ = true;
            accepted_ = true;
        }
        else
#endif
            if (result != WS_SUCCESS)
        {
            if (!tolerate(result))
            {
                error_ = wolfSSH_get_error(ssh_);
                close();
            }
            return;
        }
        accepted_ = true;
    }

#if defined(WOLFSSH_SFTP)
    if (sftp_)
    {
        // Several requests per tick: one 32 KiB record per frame capped transfers
        // far below what the network allows. The service's IO budget still bounds it.
        for (unsigned pass = 0; pass < 8; ++pass)
        {
            const int result = wolfSSH_SFTP_read(ssh_);
            const int status = result == WS_FATAL_ERROR ? wolfSSH_get_error(ssh_) : result;
            // Upstream retains the exact partial SFTP packet while the channel
            // window is full. Yield until a later tick receives window credit.
            if (status == WS_WINDOW_FULL)
                return;
            // A clean end needs exit status and close, or scp reports failure.
            if (status == WS_EOF)
            {
                wolfSSH_SetExitStatus(ssh_, 0);
                draining_ = true;
                return;
            }
            // A dropped request would leave the client waiting forever.
            if (result == WS_FATAL_ERROR && status == WS_SUCCESS)
            {
                error_ = WS_FATAL_ERROR;
                close();
                return;
            }
            if (!tolerate(status))
            {
                error_ = status;
                close();
                return;
            }
            if (status == WS_WANT_READ || status == WS_WANT_WRITE)
                return;
        }
        return;
    }
#endif
    reap_sessions();
    const int result = wolfSSH_worker(ssh_, nullptr);
    if (!tolerate(result))
    {
        close();
        return;
    }
    reap_sessions();
    // Start at a different slot each frame so the shared IO budget is fair.
    for (unsigned pass = 0; pass < sessions_.size() && ssh_; ++pass)
    {
        auto &session = sessions_[(next_session_ + pass) % sessions_.size()];
        if (!session)
            continue;
        auto *channel = wolfSSH_ChannelFind(ssh_, session->id, WS_CHANNEL_ID_SELF);
        if (channel)
            tick_session(*session, channel);
    }
    next_session_ = (next_session_ + 1) % sessions_.size();
}
void Connection::finish_session(Session &session, WOLFSSH_CHANNEL *channel) noexcept
{
    const std::uint64_t closing_for =
        now_ < session.close_started ? ~0ULL : now_ - session.close_started;
    if (closing_for > 20000000ULL)
    {
        // The peer ignored our close as well as its window: it is not a working client.
        error_ = WS_FATAL_ERROR;
        close();
        return;
    }
    if (closing_for > 10000000ULL && !session.close_sent &&
        (session.output_size || session.error_size))
    {
        // This channel's reader stopped. Drop its unsent tail and close only this channel;
        // its siblings on the same connection keep running.
        session.output_size = session.error_size = 0;
        session.exit_status = session.exit_status ? session.exit_status : 1;
    }
    if (session.close_sent)
        return;
    if (!send_pending(session, channel, false) || !send_pending(session, channel, true))
    {
        close();
        return;
    }
    if (session.output_size || session.error_size)
        return;
    const int status =
        wolfSSH_ChannelSendExitStatus(channel, static_cast<word32>(session.exit_status));
    if (status != WS_SUCCESS)
    {
        if (!tolerate(status))
            close();
        return;
    }
    const int result = wolfSSH_ChannelExit(channel);
    if (result == WS_SUCCESS)
        session.close_sent = true;
    else if (!tolerate(result))
        close();
}
void Connection::tick_session(Session &session, WOLFSSH_CHANNEL *channel) noexcept
{
    if (session.closing)
    {
        finish_session(session, channel);
        return;
    }
    if (!session.started && now_ - session.opened_at > 15000000ULL)
    {
        session.exit_status = 1;
        session.closing = true;
        session.close_started = now_;
        return;
    }
    if (!session.started)
    {
        if (!wolfSSH_ChannelGetSessionGranted(channel))
            return;
        const auto type = wolfSSH_ChannelGetSessionType(channel);
        if (type != WOLFSSH_SESSION_EXEC && type != WOLFSSH_SESSION_SHELL)
        {
            close();
            return;
        }
        const char *command = wolfSSH_ChannelGetSessionCommand(channel);
        const std::string_view text =
            command ? std::string_view(command, wolfSSH_ChannelGetSessionCommandSz(channel))
                    : std::string_view{};
        if (linux_login_)
        {
            // The command stays in wolfSSH; tick_guest reads it when opening.
            session.guest_wait_started = now_;
            accepted_ = true;
        }
        else if (type == WOLFSSH_SESSION_EXEC && config_.update_key &&
                 (text.starts_with("ssh-key-add ") || text.starts_with("ssh-key-remove ")))
        {
            const bool remove = text.starts_with("ssh-key-remove ");
            const std::size_t offset = remove ? 15 : 12;
            session.managed = true;
            const bool ok = config_.update_key(
                config_.key_context, {text.data() + offset, text.size() - offset}, remove);
            session.exit_status = ok ? 0 : 1;
            const std::string_view message =
                ok ? "SSH key updated. New connections use the updated keys.\n"
                   : "Key update refused: invalid/changed file, unsupported key, key limit or IO "
                     "error.\n";
            auto &buffer = ok ? session.output : session.errors;
            auto &size = ok ? session.output_size : session.error_size;
            std::copy(message.begin(), message.end(), buffer.begin());
            size = message.size();
        }
        else if (type == WOLFSSH_SESSION_EXEC && session.has_pty && warp_bootstrap(text))
            session.command.emplace(SessionMode::shell, std::string_view{}, config_.directory);
        else
            session.command.emplace(type == WOLFSSH_SESSION_EXEC ? SessionMode::exec
                                                                 : SessionMode::shell,
                                    text, config_.directory);
        session.started = true;
    }
    if (linux_login_)
    {
        tick_guest(session, channel);
        return;
    }
    if (session.managed)
    {
        if (!send_pending(session, channel, false) || !send_pending(session, channel, true))
        {
            close();
            return;
        }
        if (!session.output_size && !session.error_size)
        {
            session.closing = true;
            session.close_started = now_;
        }
        return;
    }
    bool channel_input_empty = false;
    if (!session.input_size)
    {
        const int count = wolfSSH_ChannelRead(
            channel, reinterpret_cast<byte *>(session.input.data()), session.input.size());
        if (count < 0)
        {
            if (!tolerate(count))
                close();
            return;
        }
        session.input_size = static_cast<std::size_t>(count);
        channel_input_empty = count == 0;
    }
    if (session.input_size)
    {
        const auto count = session.command->write({session.input.data(), session.input_size});
        session.input_size -= count;
        std::memmove(session.input.data(), session.input.data() + count, session.input_size);
        // Exec deliberately consumes no stdin. Discard it without changing the command.
        if (wolfSSH_ChannelGetSessionType(channel) == WOLFSSH_SESSION_EXEC)
            session.input_size = 0;
    }
    // EOF is latched on arrival, even while wolfSSH still owns unread data.
    // Drain both the transport buffer and our retained tail before forwarding it.
    if (channel_input_empty && !session.input_size && wolfSSH_ChannelGetEof(channel))
        session.command->finish_input();
    // A few rounds per tick: one 4 KiB record per frame capped output near 100 KB/s.
    for (unsigned pass = 0; pass < 4; ++pass)
    {
        session.command->tick();
        if (!send_pending(session, channel, false) || !send_pending(session, channel, true))
        {
            close();
            return;
        }
        if (session.output_size || session.error_size)
            break; // The client's window or socket is full.
    }
    if (session.command->finished() && !session.output_size && !session.error_size)
    {
        session.exit_status = session.command->exit_status();
        session.closing = true;
        session.close_started = now_;
    }
}

void Connection::end_guest(Session &session, int status, std::string_view message) noexcept
{
    const auto count = std::min(message.size(), session.output.size() - session.output_size);
    std::copy_n(message.data(), count, session.output.data() + session.output_size);
    session.output_size += count;
    if (session.guest_handle > 0)
        config_.guest->close(session.guest_handle);
    session.guest_handle = 0;
    session.exit_status = status;
    session.closing = true;
    session.close_started = now_;
}
// A real PTY inside Linux: bytes pass through unchanged in both directions, the
// guest's line discipline handles echo, Ctrl+C and line endings.
void Connection::tick_guest(Session &session, WOLFSSH_CHANNEL *channel) noexcept
{
    if (session.guest_handle == 0)
    {
        const bool exec = wolfSSH_ChannelGetSessionType(channel) == WOLFSSH_SESSION_EXEC;
        const char *command = exec ? wolfSSH_ChannelGetSessionCommand(channel) : nullptr;
        const int opened = config_.guest->open(
            session.pty_cols, session.pty_rows, session.pty_term.data(),
            command ? std::string_view(command, wolfSSH_ChannelGetSessionCommandSz(channel))
                    : std::string_view{},
            session.has_pty);
        if (opened == guest_link_failed)
        {
            end_guest(session, 1,
                      "The link to Linux failed. On the console, use Settings > Stop Linux, then "
                      "start it again.\r\n");
            return;
        }
        if (opened < 0)
        {
            end_guest(session, 1, "Linux is not available in this build.\r\n");
            return;
        }
        if (opened == 0)
        {
            if (!session.guest_notice && !exec)
            {
                session.guest_notice = true;
                static constexpr char notice[] =
                    "Starting Linux on the console; this takes a few seconds...\r\n";
                (void)wolfSSH_ChannelSend(channel,
                                          reinterpret_cast<byte *>(const_cast<char *>(notice)),
                                          sizeof(notice) - 1);
            }
            if (now_ - session.guest_wait_started > 120000000ULL)
                end_guest(session, 1, "Linux did not become ready.\r\n");
            return;
        }
        session.guest_handle = opened;
    }
    bool channel_input_empty = false;
    if (!session.input_size)
    {
        const int count = wolfSSH_ChannelRead(
            channel, reinterpret_cast<byte *>(session.input.data()), session.input.size());
        if (count < 0)
        {
            if (!tolerate(count))
                close();
            return;
        }
        session.input_size = static_cast<std::size_t>(count);
        channel_input_empty = count == 0;
    }
    if (session.input_size)
    {
        const auto count =
            config_.guest->write(session.guest_handle, {session.input.data(), session.input_size});
        session.input_size -= count;
        std::memmove(session.input.data(), session.input.data() + count, session.input_size);
    }
    // echo data | ssh linux@ps5 'cat > file' needs its end of file. A terminal client's
    // half-close, on the other hand, must not end an interactive shell.
    if (!session.guest_eof_sent && channel_input_empty && !session.input_size && !session.has_pty &&
        wolfSSH_ChannelGetEof(channel))
    {
        session.guest_eof_sent = true;
        config_.guest->finish_input(session.guest_handle);
    }
    for (unsigned pass = 0; pass < 4; ++pass)
    {
        if (!session.output_size)
            session.output_size = config_.guest->read(session.guest_handle, session.output.data(),
                                                      session.output.size());
        if (!session.output_size)
            break;
        const int sent =
            wolfSSH_ChannelSend(channel, reinterpret_cast<byte *>(session.output.data()),
                                static_cast<word32>(session.output_size));
        if (sent > 0)
        {
            if (static_cast<std::size_t>(sent) > session.output_size)
            {
                close();
                return;
            }
            session.output_size -= static_cast<std::size_t>(sent);
            std::memmove(session.output.data(), session.output.data() + sent, session.output_size);
            continue;
        }
        const int status = sent == WS_FATAL_ERROR ? wolfSSH_get_error(ssh_) : sent;
        if (status == WS_WANT_READ || status == WS_WANT_WRITE || status == WS_REKEYING ||
            status == WS_WINDOW_FULL || status == WS_SUCCESS)
            break;
        error_ = status;
        close();
        return;
    }
    int exit_status = 0;
    const int state = config_.guest->poll(session.guest_handle, &exit_status);
    if (state == 1 && !session.output_size)
        end_guest(session, exit_status, {});
    else if (state < 0)
        end_guest(session, 255, "\r\nLinux stopped on the console.\r\n");
}
} // namespace term::ssh
