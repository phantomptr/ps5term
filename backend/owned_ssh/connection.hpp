// PS5Term owned SSH transport prototype. GPL-3.0-or-later.
#pragma once
#include "src/ssh/command_session.hpp"
#include "identity.hpp"
#include "guest_access.hpp"
#include <wolfssh/ssh.h>
#include <array>
#include <optional>
#include <memory>
#include <span>
#include <string_view>

namespace term::ssh
{
// The owner supplies nonblocking IO and enforces connection/deadline limits.
// Callbacks return wolfSSH CBIO statuses. No socket/process/global-fd ownership.
struct TransportIO
{
    void *context;
    int (*receive)(void *, unsigned char *, unsigned);
    int (*send)(void *, const unsigned char *, unsigned);
};
struct ServerConfig
{
    std::string_view username, password, directory;
    std::span<const unsigned char> host_key_der;
    std::span<const AuthorizedKey> authorized_keys{};
    void *key_context = nullptr;
    bool (*update_key)(void *, std::string_view, bool remove) = nullptr;
    GuestAccess *guest = nullptr; // The login name "linux" opens a guest PTY through this.
};
// wolfSSH_Init/Cleanup belong to the enclosing single-threaded service lifetime.
// Config views must outlive this connection. At most four independent native/Linux
// channels per transport; SFTP owns an exclusive transport.
class Connection
{
  public:
    Connection(const ServerConfig &, TransportIO) noexcept;
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    void tick() noexcept;
    void close() noexcept;
    bool closed() const noexcept
    {
        return !ssh_;
    }
    bool authenticated() const noexcept
    {
        return accepted_;
    }
    bool draining() const noexcept
    {
        return draining_;
    }
    // A terminal session may sit quiet for hours; a dead peer is TCP keepalive's job.
    bool long_lived() const noexcept;
    void set_time(std::uint64_t monotonic_us) noexcept
    {
        now_ = monotonic_us;
    }
    unsigned password_failures() const noexcept
    {
        return password_failures_;
    }
    int last_error() const noexcept
    {
        return error_;
    }

  private:
    ServerConfig config_;
    TransportIO io_;
    WOLFSSH_CTX *context_ = nullptr;
    WOLFSSH *ssh_ = nullptr;
    struct Session
    {
        word32 id = 0;
        std::optional<CommandSession> command;
        bool requested = false, started = false, closing = false, close_sent = false;
        bool managed = false, has_pty = false, guest_notice = false, guest_eof_sent = false;
        int exit_status = 0, guest_handle = 0;
        std::array<char, 64> pty_term{};
        unsigned pty_cols = 80, pty_rows = 24;
        char last_output = 0;
        std::uint64_t opened_at = 0, guest_wait_started = 0, close_started = 0;
        std::array<char, 4096> input{}, output{}, errors{};
        std::size_t input_size = 0, output_size = 0, error_size = 0;
    };
    std::array<std::unique_ptr<Session>, WOLFSSH_MAX_SESSION_CHANNELS> sessions_{};
    unsigned next_session_ = 0;
    bool accepted_ = false, draining_ = false, linux_login_ = false;
    int error_ = 0;
    unsigned password_failures_ = 0;
    std::uint64_t now_ = 0;
#if defined(WOLFSSH_SFTP)
    bool sftp_ = false;
#endif
    Session *find_session(WOLFSSH_CHANNEL *) noexcept;
    void release_session(Session &) noexcept;
    void reap_sessions() noexcept;
    void tick_session(Session &, WOLFSSH_CHANNEL *) noexcept;
    void finish_session(Session &, WOLFSSH_CHANNEL *) noexcept;
    void tick_guest(Session &, WOLFSSH_CHANNEL *) noexcept;
    void end_guest(Session &, int status, std::string_view message) noexcept;
    static int auth(byte, WS_UserAuthData *, void *);
    static int auth_types(WOLFSSH *, void *);
    static int channel_open(WOLFSSH_CHANNEL *, void *);
    static int request(WOLFSSH_CHANNEL *, const byte *, word32, const byte *, word32, int, void *);
    static int receive(WOLFSSH *, void *, word32, void *);
    static int send(WOLFSSH *, void *, word32, void *);
    bool send_pending(Session &, WOLFSSH_CHANNEL *, bool error_stream) noexcept;
    bool tolerate(int) const noexcept;
};
} // namespace term::ssh
