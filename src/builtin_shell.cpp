// PS5Term - GPL-3.0-or-later.
#include "builtin_shell.hpp"
#include "platform/system_info.hpp"
#include <csignal>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#if defined(__PROSPERO__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#endif
#ifndef __PROSPERO__
#include <sys/utsname.h>
#else
extern "C" [[noreturn]] void abort();
// PS5 libc++ headers declare __libcpp_verbose_abort but the stub libraries
// omit the symbol. Provide a minimal definition that just calls abort().
// The previous version placed this in namespace std::__1 which may conflict
// with the PS5 runtime's own weak definition. Use the exact mangled name
// instead, avoiding any namespace collision.
extern "C" [[noreturn]] void _ZNSt3__122__libcpp_verbose_abortEPKcz(const char *, ...)
{
    abort();
}
#endif
#include <unistd.h>
namespace term
{
namespace
{
constexpr std::size_t copy_chunk = 64 * 1024, ls_batch = 1024, xxd_default = 64 * 1024;
constexpr const char *home_directory = "/data";
// Native app contexts can reject lstat for ordinary readable files. Match the
// owned SFTP adapter's no-follow descriptor fallback; never claim link metadata
// from a followed target and never open special devices for this operation.
int metadata(const char *path, struct stat *result)
{
    if (lstat(path, result) == 0)
        return 0;
#if defined(__PROSPERO__)
    struct stat preliminary
    {
    };
    if (stat(path, &preliminary) < 0)
        return -1;
    if (!S_ISREG(preliminary.st_mode) && !S_ISDIR(preliminary.st_mode))
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return -1;
    int rc = fstat(fd, result), saved = errno;
    if (rc == 0 && !S_ISREG(result->st_mode) && !S_ISDIR(result->st_mode))
    {
        rc = -1;
        saved = EOPNOTSUPP;
    }
    if (close(fd) < 0 && rc == 0)
        return -1;
    errno = saved;
    return rc;
#else
    return -1;
#endif
}
// Read a whole regular file, bounded by cap. Nonblocking open: a FIFO must fail
// with an error rather than freeze the UI thread waiting for a writer.
ssize_t slurp(const char *path, char *buf, std::size_t cap)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct stat st
    {
    };
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))
    {
        const auto saved = errno;
        close(fd);
        errno = S_ISDIR(st.st_mode) ? EISDIR : saved ? saved : EINVAL;
        return -1;
    }
    std::size_t total = 0;
    while (total < cap)
    {
        auto n = ::read(fd, buf + total, std::min(cap - total, copy_chunk));
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
            {
                const auto saved = errno;
                close(fd);
                errno = saved;
                return -1;
            }
            break;
        }
        total += static_cast<std::size_t>(n);
    }
    if (total == cap)
    {
        char extra;
        ssize_t n;
        do
        {
            n = ::read(fd, &extra, 1);
        } while (n < 0 && errno == EINTR);
        if (n != 0)
        {
            const auto saved = n > 0 ? EFBIG : errno;
            close(fd);
            errno = saved;
            return -1;
        }
    }
    close(fd);
    return static_cast<ssize_t>(total);
}
bool write_all(int fd, const char *data, std::size_t size)
{
    std::size_t written = 0;
    while (written < size)
    {
        const auto n = ::write(fd, data + written, size - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            if (n == 0)
                errno = EIO;
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}
const char *find_substr(const char *hay, std::size_t hlen, const char *ndl, std::size_t nlen)
{
    if (!nlen)
        return hay;
    if (nlen > hlen)
        return nullptr;
    for (std::size_t i = 0; i <= hlen - nlen; ++i)
        if (std::memcmp(hay + i, ndl, nlen) == 0)
            return hay + i;
    return nullptr;
}
const char *find_substr_i(const char *hay, std::size_t hlen, const char *ndl, std::size_t nlen)
{
    if (!nlen)
        return hay;
    if (nlen > hlen)
        return nullptr;
    for (std::size_t i = 0; i <= hlen - nlen; ++i)
    {
        bool ok = true;
        for (std::size_t j = 0; j < nlen; ++j)
            if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
                std::tolower(static_cast<unsigned char>(ndl[j])))
            {
                ok = false;
                break;
            }
        if (ok)
            return hay + i;
    }
    return nullptr;
}
bool parse_count(const char *text, unsigned long &value) noexcept
{
    if (!std::isdigit(static_cast<unsigned char>(*text)))
        return false;
    char *end = nullptr;
    value = std::strtoul(text, &end, 10); // Overflow saturates, which still means "all".
    return end && !*end;
}
// Line count option shared by head and tail: -n N, -nN, -N. plus reports +N.
bool line_option(int argc, char **argv, int &arg, unsigned long &lines, bool *plus) noexcept
{
    if (arg >= argc || argv[arg][0] != '-' || !argv[arg][1])
        return true;
    const char *text = nullptr;
    if (argv[arg][1] == 'n')
    {
        text = argv[arg][2] ? argv[arg] + 2 : (++arg < argc ? argv[arg] : nullptr);
    }
    else if (std::isdigit(static_cast<unsigned char>(argv[arg][1])))
        text = argv[arg] + 1;
    if (!text)
        return false;
    if (plus && *text == '+')
    {
        *plus = true;
        ++text;
    }
    if (!parse_count(text, lines))
        return false;
    ++arg;
    return true;
}
long long leading_number(const char *text, std::size_t size) noexcept
{
    std::size_t i = 0;
    while (i < size && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    const bool negative = i < size && text[i] == '-';
    if (negative)
        ++i;
    constexpr long long limit = __LONG_LONG_MAX__ / 10 - 1;
    long long value = 0;
    for (; i < size && std::isdigit(static_cast<unsigned char>(text[i])); ++i)
    {
        if (value > limit)
            break; // Saturate rather than overflow.
        value = value * 10 + (text[i] - '0');
    }
    return negative ? -value : value;
}
struct TextLine
{
    const char *data;
    std::size_t size;
    std::size_t order;
};
constexpr std::array<std::uint32_t, 64> sha_constants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned bits) noexcept
{
    return (value >> bits) | (value << (32 - bits));
}
void sha_block(std::array<std::uint32_t, 8> &state, const unsigned char *block) noexcept
{
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
               (std::uint32_t(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
    for (int i = 16; i < 64; ++i)
    {
        const auto s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto v = state;
    for (std::size_t i = 0; i < 64; ++i)
    {
        const auto s1 = rotate_right(v[4], 6) ^ rotate_right(v[4], 11) ^ rotate_right(v[4], 25);
        const auto choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const auto first = v[7] + s1 + choice + sha_constants[i] + w[i];
        const auto s0 = rotate_right(v[0], 2) ^ rotate_right(v[0], 13) ^ rotate_right(v[0], 22);
        const auto majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + first;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = first + s0 + majority;
    }
    for (std::size_t i = 0; i < 8; ++i)
        state[i] += v[i];
}
constexpr std::array<std::uint32_t, 8> sha_initial{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
std::uint64_t monotonic_ms() noexcept
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000 +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000000;
}
// Find the next list element in s: returns its length, and the connector that ends
// it as ';', '&' for &&, '|' for ||, or 0 at the end of the line.
std::size_t list_item(const char *s, char &connector) noexcept
{
    char q = 0;
    connector = 0;
    for (std::size_t i = 0; s[i]; ++i)
    {
        const char c = s[i];
        if (c == '\\' && q != '\'')
        {
            if (s[i + 1])
                ++i;
            continue;
        }
        if (c == '\'' || c == '"')
        {
            if (!q)
                q = c;
            else if (q == c)
                q = 0;
            continue;
        }
        if (q)
            continue;
        if (c == ';')
        {
            connector = ';';
            return i;
        }
        if ((c == '&' || c == '|') && s[i + 1] == c)
        {
            connector = c;
            return i;
        }
    }
    return std::strlen(s);
}
std::size_t connector_size(char connector) noexcept
{
    return connector == ';' ? 1 : connector ? 2 : 0;
}
bool blank(const char *s, std::size_t size) noexcept
{
    for (std::size_t i = 0; i < size; ++i)
        if (s[i] != ' ' && s[i] != '\t')
            return false;
    return true;
}
} // namespace
// ---- Construction / reset ----
BuiltinShell::BuiltinShell(std::string_view directory, bool interactive) noexcept
    : interactive_(interactive)
{
    reset(directory);
}
void BuiltinShell::reset(std::string_view directory) noexcept
{
    close_resources();
    release_buffers();
    state_ = State::idle;
    cancelled_ = false;
    size_ = 0;
    errors_ = {};
    error_size_ = 0;
    exit_status_ = 0;
    io_error_ = 0;
    cwd_ = {};
    previous_cwd_ = {};
    command_ = {};
    output_ = {};
    list_position_ = 0;
    list_connector_ = ';';
    list_checked_ = false;
    cwd_[0] = '/';
    std::array<char, 1024> initial{};
    if (path(directory, initial))
        cwd_ = initial;
    previous_cwd_ = cwd_;
    pipeline_ = {};
    pipe_stage_ = 0;
    pipe_in_size_ = 0;
    pipe_out_size_ = 0;
    pipe_which_ = false;
    piping_ = false;
    env_count_ = 0;
    env_ = {};
}
BuiltinShell::~BuiltinShell()
{
    close_resources();
    release_buffers();
}
char *BuiltinShell::buffer(Slot slot) noexcept
{
    if (!buffers_[slot])
    {
        buffers_[slot] = new (std::nothrow) char[text_limit];
        if (!buffers_[slot])
            io_error_ = ENOMEM;
    }
    return buffers_[slot];
}
void BuiltinShell::release_buffers() noexcept
{
    for (auto &slot : buffers_)
    {
        delete[] slot;
        slot = nullptr;
    }
    spill_size_ = spill_sent_ = 0;
    spill_then_complete_ = false;
}
// ---- Path resolution ----
bool BuiltinShell::path(std::string_view value, std::array<char, 1024> &result,
                        bool normalize) const noexcept
{
    if (value.empty())
    {
        errno = ENOENT;
        return false;
    }
    std::array<char, 2048> input{};
    std::string_view base =
        value.starts_with('/') ? std::string_view{} : std::string_view(cwd_.data());
    if (value == "~" || value.starts_with("~/"))
    {
        base = home_directory;
        value.remove_prefix(1);
        if (value.empty())
            value = "/";
    }
    if (value.find('\0') != value.npos || base.size() + 1 + value.size() >= input.size())
    {
        errno = ENAMETOOLONG;
        return false;
    }
    std::size_t length = 0;
    for (char ch : base)
        input[length++] = ch;
    if (!value.starts_with('/'))
        input[length++] = '/';
    for (char ch : value)
        input[length++] = ch;
    if (!normalize)
    {
        if (length >= result.size())
        {
            errno = ENAMETOOLONG;
            return false;
        }
        result = {};
        std::copy_n(input.data(), length, result.data());
        return true;
    }
    result = {};
    result[0] = '/';
    std::size_t used = 1;
    for (std::size_t i = 0; i < length;)
    {
        while (i < length && input[i] == '/')
            ++i;
        const auto first = i;
        while (i < length && input[i] != '/')
            ++i;
        const std::string_view component(input.data() + first, i - first);
        if (component.empty() || component == ".")
            continue;
        if (component == "..")
        {
            while (used > 1 && result[used - 1] != '/')
                result[--used] = 0;
            if (used > 1)
                result[--used] = 0;
            continue;
        }
        const auto separator = used > 1 ? 1U : 0U;
        if (used + separator + component.size() >= result.size())
        {
            errno = ENAMETOOLONG;
            return false;
        }
        if (separator)
            result[used++] = '/';
        for (char ch : component)
            result[used++] = ch;
    }
    return true;
}
// ---- Core I/O helpers ----
void BuiltinShell::program_finished(std::string_view message, int status) noexcept
{
    if (state_ != State::program)
        return;
    program_taken_ = false;
    exit_status_ = status;
    append(message);
    finish();
}
bool BuiltinShell::busy() const noexcept
{
    return state_ != State::idle || cancelled_ || spill_size_;
}
bool BuiltinShell::submit(std::string_view command) noexcept
{
    if (busy() || size_ || error_size_ || command.size() >= command_.size() ||
        command.find('\0') != command.npos || command.find('\r') != command.npos ||
        command.find('\n') != command.npos)
        return false;
    command_ = {};
    std::copy(command.begin(), command.end(), command_.begin());
    list_position_ = 0;
    list_connector_ = ';';
    list_checked_ = false;
    state_ = State::pending;
    return true;
}
void BuiltinShell::close_resources() noexcept
{
    filename_expansion_.reset();
    disk_usage_.reset();
    directory_.close();
    if (file_ >= 0)
        close(file_);
    file_ = -1;
    if (copy_out_ >= 0)
        close(copy_out_);
    copy_out_ = -1;
    if (redir_fd_ >= 0)
        close(redir_fd_);
    redir_fd_ = -1;
    if (err_fd_ >= 0)
        close(err_fd_);
    err_fd_ = -1;
}
void BuiltinShell::put_terminal(std::string_view value) noexcept
{
    if (!spill_size_ && value.size() <= output_.size() - size_)
    {
        std::copy(value.begin(), value.end(), output_.begin() + static_cast<std::ptrdiff_t>(size_));
        size_ += value.size();
        return;
    }
    // Larger than one read: keep it and hand it out across ticks, never lose it.
    if (value.size() > text_limit - spill_size_)
    {
        io_error_ = EOVERFLOW;
        return;
    }
    auto *spill = buffer(slot_spill);
    if (!spill)
        return;
    std::memcpy(spill + spill_size_, value.data(), value.size());
    spill_size_ += value.size();
}
void BuiltinShell::append(std::string_view value) noexcept
{
    if (io_error_ || value.empty())
        return;
    wrote_ = true;
    last_byte_ = value.back();
    if (piping_)
    {
        if (value.size() > text_limit - pipe_out_size_)
        {
            io_error_ = EOVERFLOW;
            return;
        }
        auto *destination = pipe_out_ptr();
        if (!destination)
            return;
        std::memcpy(destination + pipe_out_size_, value.data(), value.size());
        pipe_out_size_ += value.size();
        return;
    }
    if (redir_fd_ >= 0)
    {
        if (!write_all(redir_fd_, value.data(), value.size()))
            io_error_ = errno;
        return;
    }
    put_terminal(value);
}
// Errors go to the user, not into the pipe or file that receives normal output.
void BuiltinShell::report(std::string_view message) noexcept
{
    if (pipeline_.err_to_out)
    {
        append(message);
        return;
    }
    if (err_fd_ >= 0)
    {
        (void)write_all(err_fd_, message.data(), message.size());
        return;
    }
    if (interactive_)
    {
        put_terminal(message);
        return;
    }
    const auto count = std::min(message.size(), errors_.size() - error_size_);
    std::copy_n(message.data(), count, errors_.data() + error_size_);
    error_size_ += count;
}
void BuiltinShell::finish() noexcept
{
    disk_usage_.reset();
    // Streaming input belongs to a stage; output redirection belongs to the
    // whole pipeline and must survive advancing to the final stage.
    directory_.close();
    if (file_ >= 0)
        close(file_);
    file_ = -1;
    if (copy_out_ >= 0)
        close(copy_out_);
    copy_out_ = -1;
    state_ = State::idle;
    if (io_error_)
    {
        const auto failure = io_error_;
        io_error_ = 0;
        pipeline_.count = 0;
        piping_ = false;
        if (failure == EOVERFLOW)
            report("shell: output exceeds the 512 KiB limit; command failed\n");
        else if (failure == ENOMEM)
            report("shell: not enough memory for this command\n");
        else
        {
            char message[128];
            std::snprintf(message, sizeof message, "shell: %s\n", std::strerror(failure));
            report(message);
        }
        exit_status_ = 1;
    }
    if (pipeline_.count > 1 && pipe_stage_ < pipeline_.count - 1)
    {
        advance_pipe();
        return;
    }
    if (redir_fd_ >= 0)
    {
        if (close(redir_fd_) < 0 && !exit_status_)
            exit_status_ = 1;
        redir_fd_ = -1;
    }
    if (err_fd_ >= 0)
    {
        close(err_fd_);
        err_fd_ = -1;
    }
    pipeline_.count = 0;
    pipeline_.err_to_out = 0;
    pipe_stage_ = 0;
    piping_ = false;
    pipe_out_size_ = 0;
    pipe_in_size_ = 0;
    command_name_ = nullptr;
    filename_expansion_.reset();
    if (spill_size_)
    {
        spill_then_complete_ = true;
        return;
    }
    complete_item();
}
void BuiltinShell::complete_item() noexcept
{
    if (command_[list_position_])
    {
        state_ = State::pending;
        return;
    }
    release_buffers();
    command_ = {};
    list_position_ = 0;
    if (interactive_)
    {
        put_terminal(cwd_.data());
        put_terminal("$ ");
    }
}
void BuiltinShell::diagnostic(std::string_view message, int status) noexcept
{
    report(message);
    exit_status_ = status;
    finish();
}
void BuiltinShell::usage(const char *text) noexcept
{
    char message[160];
    std::snprintf(message, sizeof message, "usage: %s\n", text);
    diagnostic(message, 2);
}
void BuiltinShell::error(int code) noexcept
{
    char message[192];
    std::snprintf(message, sizeof message, "%s: %s\n", command_name_ ? command_name_ : "shell",
                  std::strerror(code));
    diagnostic(message, 1);
}
std::size_t BuiltinShell::read_error(char *destination, std::size_t capacity) noexcept
{
    if (!destination)
        return 0;
    const auto count = std::min(capacity, error_size_);
    std::copy_n(errors_.data(), count, destination);
    std::memmove(errors_.data(), errors_.data() + count, error_size_ - count);
    std::fill(errors_.begin() + static_cast<std::ptrdiff_t>(error_size_ - count),
              errors_.begin() + static_cast<std::ptrdiff_t>(error_size_), 0);
    error_size_ -= count;
    return count;
}
std::size_t BuiltinShell::read(char *destination, std::size_t capacity) noexcept
{
    if (!destination)
        return 0;
    const auto count = std::min(capacity, size_);
    std::copy_n(output_.data(), count, destination);
    std::memmove(output_.data(), output_.data() + count, size_ - count);
    std::fill(output_.begin() + static_cast<std::ptrdiff_t>(size_ - count),
              output_.begin() + static_cast<std::ptrdiff_t>(size_), 0);
    size_ -= count;
    return count;
}
bool BuiltinShell::interrupt() noexcept
{
    if (!busy())
        return false;
    close_resources();
    state_ = State::idle;
    cancelled_ = true;
    exit_status_ = 130;
    io_error_ = 0;
    pipeline_.count = 0;
    pipeline_.err_to_out = 0;
    pipe_stage_ = 0;
    piping_ = false;
    pipe_out_size_ = 0;
    pipe_in_size_ = 0;
    command_name_ = nullptr;
    spill_size_ = spill_sent_ = 0;
    spill_then_complete_ = false;
    command_ = {};
    list_position_ = 0;
    return true;
}
// ---- Environment variables ----
const char *BuiltinShell::env_get(std::string_view key) const noexcept
{
    for (int i = 0; i < env_count_; ++i)
        if (std::string_view(env_[i].key) == key)
            return env_[i].value;
    if (key == "PWD")
        return cwd_.data();
    if (key == "HOME")
        return home_directory;
    return nullptr;
}
bool BuiltinShell::env_set(std::string_view key, std::string_view value) noexcept
{
    if (key.empty() || key.size() >= sizeof(EnvEntry::key) ||
        value.size() >= sizeof(EnvEntry::value))
        return false;
    for (int i = 0; i < env_count_; ++i)
    {
        if (std::string_view(env_[i].key) == key)
        {
            std::memset(env_[i].value, 0, sizeof(EnvEntry::value));
            std::copy(value.begin(), value.end(), env_[i].value);
            return true;
        }
    }
    if (env_count_ >= max_env_)
        return false;
    auto &entry = env_[env_count_++];
    std::copy(key.begin(), key.end(), entry.key);
    std::copy(value.begin(), value.end(), entry.value);
    return true;
}
void BuiltinShell::env_unset(std::string_view key) noexcept
{
    for (int i = 0; i < env_count_; ++i)
    {
        if (std::string_view(env_[i].key) == key)
        {
            env_[i] = env_[--env_count_];
            env_[env_count_] = {};
            return;
        }
    }
}
// Values are escaped on insertion: a variable can never add operators or quotes.
bool BuiltinShell::expand_vars(const char *in, char *out, std::size_t cap) const noexcept
{
    std::size_t w = 0;
    char q = 0;
    const auto put = [&](char ch) noexcept
    {
        if (w >= cap - 1)
            return false;
        out[w++] = ch;
        return true;
    };
    const auto insert = [&](const char *value) noexcept
    {
        for (; *value; ++value)
        {
            const char ch = *value;
            const bool special = q == '"' ? (ch == '"' || ch == '\\' || ch == '$' || ch == '`')
                                          : std::strchr("\\\"'|<>&; \t", ch) != nullptr;
            if ((special && !put('\\')) || !put(ch))
                return false;
        }
        return true;
    };
    for (; *in; ++in)
    {
        const char c = *in;
        if (c == '\\' && q != '\'')
        {
            if (!in[1])
                return false;
            if (!put(c) || !put(*++in))
                return false;
            continue;
        }
        if (c == '\'' || c == '"')
        {
            if (!q)
                q = c;
            else if (q == c)
                q = 0;
            if (!put(c))
                return false;
            continue;
        }
        if (c == '$' && q != '\'')
        {
            const bool braced = in[1] == '{';
            const char *start = in + (braced ? 2 : 1);
            if (*start == '?' && (!braced || start[1] == '}'))
            {
                char num[16];
                std::snprintf(num, sizeof num, "%d", exit_status_);
                if (!insert(num))
                    return false;
                in = start + (braced ? 1 : 0);
                continue;
            }
            const char *end = start;
            while (std::isalnum(static_cast<unsigned char>(*end)) || *end == '_')
                ++end;
            if (braced && (*end != '}' || end == start))
                return false;
            if (!braced && end == start)
            {
                if (*start == '(')
                    return false;
                if (!put('$'))
                    return false;
                continue;
            }
            const char *val = env_get({start, static_cast<std::size_t>(end - start)});
            if (val && !insert(val))
                return false;
            in = braced ? end : end - 1;
            continue;
        }
        if (!put(c))
            return false;
    }
    out[w] = '\0';
    return true;
}
// ---- Pipeline helpers ----
std::string_view BuiltinShell::pipe_input() noexcept
{
    if (!pipe_in_size_)
        return {};
    auto *data = pipe_in_ptr();
    return data ? std::string_view(data, pipe_in_size_) : std::string_view{};
}
void BuiltinShell::advance_pipe() noexcept
{
    pipe_which_ = !pipe_which_;
    pipe_in_size_ = pipe_out_size_;
    pipe_out_size_ = 0;
    ++pipe_stage_;
    piping_ = (pipe_stage_ < pipeline_.count - 1);
    exit_status_ = 0;
    execute_command(pipeline_.stages[pipe_stage_].argc, pipeline_.stages[pipe_stage_].argv);
}
// Text for a filter: one file operand, or the pipe or < file. An empty pipe is
// ordinary empty input; only the absence of any input is a usage error.
bool BuiltinShell::input_text(int arg, int argc, char **argv, std::string_view &text) noexcept
{
    if (argc - arg > 1)
    {
        char m[96];
        std::snprintf(m, sizeof m, "%s: expected one file operand\n", argv[0]);
        diagnostic(m, 2);
        return false;
    }
    if (arg < argc)
    {
        std::array<char, 1024> target{};
        if (!path(argv[arg], target, false))
        {
            error(errno);
            return false;
        }
        auto *data = buffer(slot_file);
        if (!data)
        {
            finish();
            return false;
        }
        const auto n = slurp(target.data(), data, text_limit);
        if (n < 0)
        {
            if (errno == EFBIG)
            {
                char m[96];
                std::snprintf(m, sizeof m, "%s: file exceeds the 512 KiB limit\n", argv[0]);
                diagnostic(m, 1);
            }
            else
                error(errno);
            return false;
        }
        text = {data, static_cast<std::size_t>(n)};
        return true;
    }
    if (!has_stdin())
    {
        char m[96];
        std::snprintf(m, sizeof m, "%s: expected a file or piped input\n", argv[0]);
        diagnostic(m, 2);
        return false;
    }
    text = pipe_input();
    if (io_error_)
    {
        finish();
        return false;
    }
    return true;
}
// ---- cat ----
void BuiltinShell::cat_next() noexcept
{
    while (operand_ < argc_)
    {
        std::array<char, 1024> target{};
        const char *name = argv_[operand_++];
        if (!path(name, target, false))
        {
            const int e = errno;
            char m[192];
            std::snprintf(m, sizeof m, "cat: %s: %s\n", name, std::strerror(e));
            report(m);
            exit_status_ = 1;
            continue;
        }
        struct stat info
        {
        };
        file_ = open(target.data(), O_RDONLY | O_NONBLOCK);
        const int opened = errno;
        if (file_ < 0 || fstat(file_, &info) < 0 || !S_ISREG(info.st_mode))
        {
            const int e = file_ < 0 ? opened : S_ISDIR(info.st_mode) ? EISDIR : EINVAL;
            if (file_ >= 0)
                close(file_);
            file_ = -1;
            char m[192];
            std::snprintf(m, sizeof m, "cat: %s: %s\n", name, std::strerror(e));
            report(m);
            exit_status_ = 1;
            continue;
        }
        if (redir_fd_ >= 0)
        {
            // cat f >> f would feed itself forever.
            struct stat out
            {
            };
            if (fstat(redir_fd_, &out) == 0 && out.st_dev == info.st_dev &&
                out.st_ino == info.st_ino)
            {
                close(file_);
                file_ = -1;
                char m[192];
                std::snprintf(m, sizeof m, "cat: %s: input file is output file\n", name);
                report(m);
                exit_status_ = 1;
                continue;
            }
        }
        state_ = State::reading;
        return;
    }
    // Keep the prompt on its own line without changing what a pipe, file or exec reader gets.
    const int status = exit_status_;
    if (interactive_ && !piping_ && redir_fd_ < 0 && wrote_ && last_byte_ != '\n')
        append("\n");
    finish();
    if (state_ == State::idle || state_ == State::pending)
        exit_status_ = status;
}
// ---- ls ----
void BuiltinShell::ls_print(const char *directory, const char *name) noexcept
{
    if (!ls_long_)
    {
        append(name);
        append("\n");
        return;
    }
    std::array<char, 1024> full{};
    int n = directory ? std::snprintf(full.data(), full.size(), "%s/%s", directory, name)
                      : std::snprintf(full.data(), full.size(), "%s", name);
    struct stat st
    {
    };
    char line[128];
    if (n < 0 || static_cast<std::size_t>(n) >= full.size() || metadata(full.data(), &st) < 0)
    {
        append("?---------        ?        ?            ?                  ");
        append(name);
        append("\n");
        return;
    }
    const auto mode = st.st_mode;
    const char type = S_ISDIR(mode)    ? 'd'
                      : S_ISLNK(mode)  ? 'l'
                      : S_ISCHR(mode)  ? 'c'
                      : S_ISBLK(mode)  ? 'b'
                      : S_ISFIFO(mode) ? 'p'
                      : S_ISSOCK(mode) ? 's'
                                       : '-';
    char perms[11] = {type, 0};
    for (int i = 0; i < 9; ++i)
        perms[i + 1] = (mode & (0400 >> i)) ? "rwx"[i % 3] : '-';
    perms[10] = 0;
    char size[24];
    const auto bytes = static_cast<unsigned long long>(st.st_size);
    if (!ls_human_ || bytes < 1024)
        std::snprintf(size, sizeof size, "%llu", bytes);
    else
    {
        double value = static_cast<double>(bytes) / 1024;
        const char *unit = "KMGTP";
        while (value >= 1024 && unit[1])
        {
            value /= 1024;
            ++unit;
        }
        std::snprintf(size, sizeof size, "%.1f%c", value, *unit);
    }
    char when[32] = "";
    const time_t modified = st.st_mtime;
    if (const struct tm *tm = std::localtime(&modified))
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
    std::snprintf(line, sizeof line, "%s %8u %8u %12s  %s  ", perms,
                  static_cast<unsigned>(st.st_uid), static_cast<unsigned>(st.st_gid), size, when);
    append(line);
    append(name);
    append("\n");
}
// Names grow from the front of the buffer, their offsets from the back.
void BuiltinShell::ls_flush() noexcept
{
    auto *names = buffers_[slot_file];
    if (!names || !ls_count_)
    {
        ls_names_ = ls_count_ = 0;
        return;
    }
    auto *offsets = reinterpret_cast<std::uint32_t *>(names + text_limit) - ls_count_;
    std::sort(offsets, offsets + ls_count_, [&](std::uint32_t a, std::uint32_t b)
              { return std::strcmp(names + a, names + b) < 0; });
    for (std::size_t i = 0; i < ls_count_ && !io_error_; ++i)
        ls_print(ls_directory_.data(), names + offsets[i]);
    ls_names_ = ls_count_ = 0;
}
void BuiltinShell::ls_next() noexcept
{
    while (operand_ < argc_)
    {
        const char *name = argv_[operand_++];
        std::array<char, 1024> target{};
        struct stat info
        {
        };
        if (!path(name, target, false) || stat(target.data(), &info) < 0)
        {
            const int e = errno;
            char m[192];
            std::snprintf(m, sizeof m, "ls: %s: %s\n", name, std::strerror(e));
            report(m);
            exit_status_ = 1;
            continue;
        }
        if (!S_ISDIR(info.st_mode))
        {
            ls_print(nullptr, target.data());
            if (io_error_)
                break;
            continue;
        }
        if (!directory_.open(target.data()))
        {
            const int e = errno;
            char m[192];
            std::snprintf(m, sizeof m, "ls: %s: %s\n", name, std::strerror(e));
            report(m);
            exit_status_ = 1;
            continue;
        }
        if (ls_header_)
        {
            append(name);
            append(":\n");
        }
        ls_directory_ = target;
        ls_names_ = ls_count_ = 0;
        if (!buffer(slot_file))
            break;
        state_ = State::listing;
        return;
    }
    const int status = exit_status_;
    finish();
    if (state_ == State::idle || state_ == State::pending)
        exit_status_ = status;
}
// ---- cp ----
bool BuiltinShell::start_copy(const char *source, const char *destination) noexcept
{
    file_ = open(source, O_RDONLY | O_NONBLOCK);
    if (file_ < 0)
    {
        error(errno);
        return false;
    }
    struct stat in
    {
    }, existing{};
    if (fstat(file_, &in) < 0)
    {
        error(errno);
        return false;
    }
    if (!S_ISREG(in.st_mode))
    {
        error(S_ISDIR(in.st_mode) ? EISDIR : EINVAL);
        return false;
    }
    // Truncate only after proving the two names differ, or cp f f empties f.
    copy_out_ = open(destination, O_WRONLY | O_CREAT | O_NONBLOCK, in.st_mode & 07777);
    if (copy_out_ < 0)
    {
        error(errno);
        return false;
    }
    if (fstat(copy_out_, &existing) < 0)
    {
        error(errno);
        return false;
    }
    if (existing.st_dev == in.st_dev && existing.st_ino == in.st_ino)
    {
        diagnostic("cp: source and destination are the same file\n", 1);
        return false;
    }
    if (S_ISREG(existing.st_mode) && ftruncate(copy_out_, 0) < 0)
    {
        error(errno);
        return false;
    }
    if (!buffer(slot_file))
    {
        finish();
        return false;
    }
    state_ = State::copying;
    return true;
}
void BuiltinShell::copy_step() noexcept
{
    auto *data = buffer(slot_file);
    if (!data)
    {
        finish();
        return;
    }
    const auto count = ::read(file_, data, copy_chunk);
    if (count < 0)
    {
        if (errno != EINTR && errno != EAGAIN)
            error(errno);
        return;
    }
    if (count == 0)
    {
        const int failed = close(copy_out_) < 0 ? errno : 0;
        copy_out_ = -1;
        if (failed)
            error(failed);
        else
            finish();
        return;
    }
    if (!write_all(copy_out_, data, static_cast<std::size_t>(count)))
        error(errno);
}
// ---- sha256sum ----
void BuiltinShell::sha_update(const unsigned char *data, std::size_t size) noexcept
{
    auto used = static_cast<std::size_t>(sha_total_ % 64);
    sha_total_ += size;
    if (used)
    {
        const auto take = std::min(size, 64 - used);
        std::memcpy(sha_block_.data() + used, data, take);
        data += take;
        size -= take;
        used += take;
        if (used < 64)
            return;
        sha_block(sha_state_, sha_block_.data());
    }
    for (; size >= 64; data += 64, size -= 64)
        sha_block(sha_state_, data);
    if (size)
        std::memcpy(sha_block_.data(), data, size);
}
void BuiltinShell::hash_step() noexcept
{
    auto *data = buffer(slot_file);
    if (!data)
    {
        finish();
        return;
    }
    const auto count = ::read(file_, data, copy_chunk);
    if (count < 0)
    {
        if (errno != EINTR && errno != EAGAIN)
            error(errno);
        return;
    }
    if (count > 0)
    {
        sha_update(reinterpret_cast<unsigned char *>(data), static_cast<std::size_t>(count));
        return;
    }
    const std::uint64_t bits = sha_total_ * 8;
    const unsigned char one = 0x80, zero = 0;
    sha_update(&one, 1);
    while (sha_total_ % 64 != 56)
        sha_update(&zero, 1);
    unsigned char length[8];
    for (int i = 0; i < 8; ++i)
        length[i] = static_cast<unsigned char>(bits >> (56 - i * 8));
    sha_update(length, 8);
    char digest[65];
    for (std::size_t i = 0; i < 8; ++i)
        std::snprintf(digest + i * 8, 9, "%08x", static_cast<unsigned>(sha_state_[i]));
    append(digest);
    append("  ");
    append(argv_[operand_ - 1]);
    append("\n");
    finish();
}
// ---- Command dispatch ----
void BuiltinShell::execute_command(int argc, char **argv) noexcept
{
    if (argc == 0)
    {
        finish();
        return;
    }
    const std::string_view cmd = argv[0];
    command_name_ = argv[0];
    wrote_ = false;
    last_byte_ = 0;
    // ---- NAME=value ----
    if (argc == 1)
    {
        if (const char *eq = std::strchr(argv[0], '='); eq && eq != argv[0])
        {
            const std::string_view key(argv[0], static_cast<std::size_t>(eq - argv[0]));
            bool valid = !std::isdigit(static_cast<unsigned char>(key[0]));
            for (char ch : key)
                valid = valid && (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_');
            if (valid)
            {
                if (!env_set(key, eq + 1))
                {
                    diagnostic("shell: variable table full or value too long\n", 1);
                    return;
                }
                finish();
                return;
            }
        }
    }
    // ---- Shell builtins ----
    if (cmd == "help")
    {
        append(
            "Built-in commands (Settings > Start Linux gives a full Linux shell):\n"
            "  files:  ls [-alh] cat cp mv rm [-f] mkdir [-p] rmdir touch chmod stat du [-shk]\n"
            "  text:   grep [-ivcn] head tail wc sort [-rn] uniq [-c] tee [-a] xxd sha256sum\n"
            "  system: pwd cd id uname [-asnrm] hostname whoami date basename dirname\n"
            "          ps [-f] kill [-SIG|-l] PID uptime free [-h] nproc sysinfo sysctl NAME\n"
            "  other:  echo printf sleep env export unset true false clear help\n"
            "  programs: run PATH [ARGS] runs a PS5 ELF program in this tab (needs an ELF\n"
            "          loader on port 9021; Ctrl+C interrupts; no TTY, so the size is fixed)\n"
            "Combine: cmd1 | cmd2, cmd1 ; cmd2, cmd1 && cmd2, cmd1 || cmd2\n"
            "Redirect: > file, >> file, < file, 2> file, 2>&1\n"
            "Wildcards: final filename * and ?; quote literal patterns; Linux has full globbing\n"
            "Variables: NAME=value, export NAME=value, $NAME, ${NAME}, $?, $PWD, $HOME; ~ is "
            "/data\n");
        finish();
        return;
    }
    if (cmd == "true")
    {
        exit_status_ = 0;
        finish();
        return;
    }
    if (cmd == "false")
    {
        exit_status_ = 1;
        finish();
        return;
    }
    if (cmd == "clear")
    {
        append("\033[2J\033[H");
        finish();
        return;
    }
    if (cmd == "pwd")
    {
        append(cwd_.data());
        append("\n");
        finish();
        return;
    }
    if (cmd == "echo")
    {
        bool newline = true;
        int start = 1;
        if (argc >= 2 && std::strcmp(argv[1], "-n") == 0)
        {
            newline = false;
            start = 2;
        }
        for (int i = start; i < argc; ++i)
        {
            if (i > start)
                append(" ");
            append(argv[i]);
        }
        if (newline)
            append("\n");
        finish();
        return;
    }
    if (cmd == "printf")
    {
        if (argc < 2)
        {
            usage("printf FORMAT [ARGUMENT...]");
            return;
        }
        int next = 2;
        for (const char *f = argv[1]; *f; ++f)
        {
            if (*f == '\\' && f[1])
            {
                ++f;
                const char ch = *f == 'n'   ? '\n'
                                : *f == 't' ? '\t'
                                : *f == 'r' ? '\r'
                                : *f == 'e' ? '\033'
                                            : *f;
                append({&ch, 1});
            }
            else if (*f == '%' && f[1])
            {
                ++f;
                if (*f == '%')
                    append("%");
                else if (*f == 's' || *f == 'd')
                {
                    if (next < argc)
                        append(argv[next++]);
                    else if (*f == 'd')
                        append("0");
                }
                else
                {
                    diagnostic("printf: only %s, %d and %% are supported\n", 2);
                    return;
                }
            }
            else
                append({f, 1});
        }
        finish();
        return;
    }
    if (cmd == "run")
    {
        if (!programs_enabled_)
        {
            diagnostic("run: PS5 programs run in PS5Term's own tabs, not over SSH\n", 1);
            return;
        }
        if (argc < 2)
        {
            usage("run PATH [ARGUMENTS]  (a PS5 ELF program, in this tab)");
            return;
        }
        if (piping_ || pipeline_.count > 1 || redir_fd_ >= 0)
        {
            diagnostic("run: a program takes the whole tab; no pipes or redirection\n", 2);
            return;
        }
        std::array<char, 1024> program{};
        struct stat st
        {
        };
        errno = 0;
        if (!path(argv[1], program) || ::stat(program.data(), &st) != 0)
        {
            error(errno ? errno : ENOENT);
            return;
        }
        if (!S_ISREG(st.st_mode))
        {
            diagnostic("run: not a program file\n", 126);
            return;
        }
        program_args_size_ = 0;
        const auto add = [this](std::string_view value)
        {
            if (program_args_size_ + value.size() + 1 > program_args_.size())
                return false;
            std::memcpy(program_args_.data() + program_args_size_, value.data(), value.size());
            program_args_size_ += value.size();
            program_args_[program_args_size_++] = '\0';
            return true;
        };
        bool fits = add(program.data());
        for (int i = 2; fits && i < argc; ++i)
            fits = add(argv[i]);
        if (!fits || argc - 1 > 32)
        {
            diagnostic("run: too many or too long arguments\n", 2);
            return;
        }
        --program_args_size_; // No separator after the last argument.
        program_taken_ = false;
        state_ = State::program;
        return;
    }
    if (cmd == "sleep")
    {
        unsigned long seconds = 0;
        if (argc != 2 || !parse_count(argv[1], seconds) || seconds > 86400)
        {
            usage("sleep SECONDS (up to 86400)");
            return;
        }
        sleep_until_ = monotonic_ms() + seconds * 1000;
        state_ = State::sleeping;
        return;
    }
    if (cmd == "id")
    {
        char msg[128];
        std::snprintf(msg, sizeof msg, "uid=%u euid=%u gid=%u egid=%u\n", (unsigned)getuid(),
                      (unsigned)geteuid(), (unsigned)getgid(), (unsigned)getegid());
        append(msg);
        finish();
        return;
    }
    if (cmd == "env")
    {
        for (int i = 0; i < env_count_; ++i)
        {
            append(env_[i].key);
            append("=");
            append(env_[i].value);
            append("\n");
        }
        finish();
        return;
    }
    if (cmd == "export")
    {
        if (argc < 2)
        {
            usage("export NAME=value...");
            return;
        }
        for (int i = 1; i < argc; ++i)
        {
            const char *eq = std::strchr(argv[i], '=');
            if (!eq)
                continue; // Every variable here is already exported.
            if (eq == argv[i])
            {
                diagnostic("export: invalid assignment\n", 2);
                return;
            }
            if (!env_set({argv[i], static_cast<std::size_t>(eq - argv[i])}, eq + 1))
            {
                diagnostic("export: table full or name or value too long\n", 1);
                return;
            }
        }
        finish();
        return;
    }
    if (cmd == "unset")
    {
        if (argc < 2)
        {
            usage("unset NAME...");
            return;
        }
        for (int i = 1; i < argc; ++i)
            env_unset(argv[i]);
        finish();
        return;
    }
    // ---- Directory / file operations ----
    if (cmd == "cd")
    {
        if (argc > 2)
        {
            usage("cd [DIRECTORY]");
            return;
        }
        std::array<char, 1024> target{};
        const bool back = argc == 2 && std::strcmp(argv[1], "-") == 0;
        if (back)
            target = previous_cwd_;
        else if (!path(argc == 2 ? std::string_view(argv[1]) : std::string_view(home_directory),
                       target))
        {
            error(errno);
            return;
        }
        struct stat info
        {
        };
        if (stat(target.data(), &info) < 0)
        {
            error(errno);
            return;
        }
        if (!S_ISDIR(info.st_mode))
        {
            error(ENOTDIR);
            return;
        }
        previous_cwd_ = cwd_;
        cwd_ = target;
        if (back)
        {
            append(cwd_.data());
            append("\n");
        }
        finish();
        return;
    }
    if (cmd == "ls")
    {
        ls_all_ = ls_long_ = ls_human_ = false;
        int arg = 1;
        for (; arg < argc && argv[arg][0] == '-' && argv[arg][1]; ++arg)
            for (const char *f = argv[arg] + 1; *f; ++f)
                switch (*f)
                {
                case 'a':
                    ls_all_ = true;
                    break;
                case 'l':
                    ls_long_ = true;
                    break;
                case 'h':
                    ls_human_ = true;
                    break;
                case '1':
                    break;
                default:
                    usage("ls [-alh1] [PATH...]");
                    return;
                }
        static char dot[] = ".";
        static char *current[] = {dot};
        const bool none = arg >= argc;
        ls_header_ = argc - arg > 1;
        argv_ = none ? current : argv + arg;
        argc_ = none ? 1 : argc - arg;
        operand_ = 0;
        exit_status_ = 0;
        ls_next();
        return;
    }
    if (cmd == "cat")
    {
        if (argc == 1)
        {
            if (!has_stdin())
            {
                usage("cat FILE... (or piped input)");
                return;
            }
            const auto text = pipe_input();
            if (io_error_)
            {
                finish();
                return;
            }
            append(text);
            finish();
            return;
        }
        argv_ = argv + 1;
        argc_ = argc - 1;
        operand_ = 0;
        exit_status_ = 0;
        cat_next();
        return;
    }
    if (cmd == "mkdir")
    {
        bool parents = false;
        int start = 1;
        if (argc >= 2 && std::strcmp(argv[1], "-p") == 0)
        {
            parents = true;
            start = 2;
        }
        if (start >= argc || argv[start][0] == '-')
        {
            usage("mkdir [-p] DIRECTORY...");
            return;
        }
        for (int i = start; i < argc; ++i)
        {
            std::array<char, 1024> target{};
            if (!path(argv[i], target, false))
            {
                error(errno);
                return;
            }
            if (parents)
                for (char *p = target.data() + 1; *p; ++p)
                    if (*p == '/')
                    {
                        *p = '\0';
                        ::mkdir(target.data(), 0777);
                        *p = '/';
                    }
            if (::mkdir(target.data(), 0777) < 0)
            {
                const int e = errno;
                struct stat info
                {
                };
                if (!(parents && e == EEXIST && stat(target.data(), &info) == 0 &&
                      S_ISDIR(info.st_mode)))
                {
                    error(e == EEXIST && parents ? ENOTDIR : e);
                    return;
                }
            }
        }
        finish();
        return;
    }
    if (cmd == "rmdir")
    {
        if (argc < 2)
        {
            usage("rmdir DIRECTORY...");
            return;
        }
        for (int i = 1; i < argc; ++i)
        {
            std::array<char, 1024> target{};
            if (!path(argv[i], target, false))
            {
                error(errno);
                return;
            }
            if (::rmdir(target.data()) < 0)
            {
                error(errno);
                return;
            }
        }
        finish();
        return;
    }
    if (cmd == "rm")
    {
        bool force = false;
        int start = 1;
        if (argc >= 2 && std::strcmp(argv[1], "-f") == 0)
        {
            force = true;
            start = 2;
        }
        if (start >= argc || argv[start][0] == '-')
        {
            usage("rm [-f] FILE... (use rmdir for directories)");
            return;
        }
        for (int i = start; i < argc; ++i)
        {
            std::array<char, 1024> target{};
            if (!path(argv[i], target, false))
            {
                error(errno);
                return;
            }
            if (::unlink(target.data()) == 0 || (force && errno == ENOENT))
                continue;
            const int e = errno;
            struct stat st
            {
            };
            if (metadata(target.data(), &st) == 0 && S_ISDIR(st.st_mode))
            {
                diagnostic("rm: is a directory (use rmdir)\n", 1);
                return;
            }
            error(e);
            return;
        }
        finish();
        return;
    }
    if (cmd == "cp" || cmd == "mv")
    {
        if (argc != 3)
        {
            usage(cmd == "cp" ? "cp SOURCE DESTINATION" : "mv SOURCE DESTINATION");
            return;
        }
        std::array<char, 1024> src{}, dst{};
        if (!path(argv[1], src, false) || !path(argv[2], dst, false))
        {
            error(errno);
            return;
        }
        struct stat info
        {
        };
        if (stat(dst.data(), &info) == 0 && S_ISDIR(info.st_mode))
        {
            // Into a directory: keep the source's name.
            std::string_view base(argv[1]);
            while (base.size() > 1 && base.back() == '/')
                base.remove_suffix(1);
            if (const auto slash = base.rfind('/'); slash != base.npos)
                base.remove_prefix(slash + 1);
            const auto used = std::strlen(dst.data());
            if (used + 1 + base.size() >= dst.size())
            {
                error(ENAMETOOLONG);
                return;
            }
            dst[used] = '/';
            std::copy(base.begin(), base.end(),
                      dst.begin() + static_cast<std::ptrdiff_t>(used + 1));
        }
        if (cmd == "cp")
        {
            (void)start_copy(src.data(), dst.data());
            return;
        }
        if (::rename(src.data(), dst.data()) < 0)
        {
            error(errno);
            return;
        }
        finish();
        return;
    }
    if (cmd == "touch")
    {
        if (argc < 2)
        {
            usage("touch FILE...");
            return;
        }
        for (int i = 1; i < argc; ++i)
        {
            std::array<char, 1024> target{};
            if (!path(argv[i], target, false))
            {
                error(errno);
                return;
            }
            int fd = open(target.data(), O_WRONLY | O_CREAT | O_NONBLOCK, 0666);
            if (fd < 0)
            {
                error(errno);
                return;
            }
            const int updated = futimes(fd, nullptr), e = errno;
            close(fd);
            if (updated < 0)
            {
                error(e);
                return;
            }
        }
        finish();
        return;
    }
    if (cmd == "chmod")
    {
        if (argc < 3)
        {
            usage("chmod OCTAL-MODE FILE...");
            return;
        }
        char *end = nullptr;
        const auto parsed = std::strtoul(argv[1], &end, 8);
        if (!*argv[1] || !end || *end || parsed > 07777)
        {
            diagnostic("chmod: invalid octal mode\n", 2);
            return;
        }
        for (int i = 2; i < argc; ++i)
        {
            std::array<char, 1024> target{};
            if (!path(argv[i], target, false))
            {
                error(errno);
                return;
            }
            if (::chmod(target.data(), static_cast<mode_t>(parsed)) < 0)
            {
                error(errno);
                return;
            }
        }
        finish();
        return;
    }
    if (cmd == "stat")
    {
        if (argc != 2)
        {
            usage("stat PATH");
            return;
        }
        std::array<char, 1024> target{};
        if (!path(argv[1], target, false))
        {
            error(errno);
            return;
        }
        struct stat st
        {
        };
        if (metadata(target.data(), &st) < 0)
        {
            error(errno);
            return;
        }
        const char *type = S_ISREG(st.st_mode)    ? "regular"
                           : S_ISDIR(st.st_mode)  ? "directory"
                           : S_ISLNK(st.st_mode)  ? "symlink"
                           : S_ISFIFO(st.st_mode) ? "fifo"
                                                  : "other";
        char msg[1280];
        std::snprintf(msg, sizeof msg,
                      "  File: %s\n  Type: %s\n  Size: %lld\n  Mode: %04o\n  Uid: %u  Gid: %u\n",
                      target.data(), type, (long long)st.st_size, (unsigned)(st.st_mode & 07777),
                      (unsigned)st.st_uid, (unsigned)st.st_gid);
        append(msg);
        char time_str[64];
        const struct tm *tmp = std::localtime(&st.st_mtime);
        struct tm tm = tmp ? *tmp : (struct tm){};
        std::strftime(time_str, sizeof time_str, "Modify: %Y-%m-%d %H:%M:%S\n", &tm);
        append(time_str);
        finish();
        return;
    }
    if (cmd == "du")
    {
        du_human_ = false;
        bool options = true;
        du_label_ = nullptr;
        for (int i = 1; i < argc; ++i)
        {
            if (options && std::strcmp(argv[i], "--") == 0)
            {
                options = false;
                continue;
            }
            if (options && argv[i][0] == '-' && argv[i][1])
            {
                for (const char *flag = argv[i] + 1; *flag; ++flag)
                {
                    if (*flag == 'h')
                        du_human_ = true;
                    else if (*flag == 'k')
                        du_human_ = false;
                    else if (*flag != 's')
                    {
                        usage("du [-shk] [path]");
                        return;
                    }
                }
            }
            else
            {
                options = false;
                if (du_label_)
                {
                    usage("du [-shk] [path]");
                    return;
                }
                du_label_ = argv[i];
            }
        }
        if (!du_label_)
            du_label_ = ".";
        std::array<char, 1024> target{};
        if (!path(du_label_, target, false))
        {
            error(errno);
            return;
        }
        disk_usage_.reset(new (std::nothrow) platform::DiskUsage(metadata));
        if (!disk_usage_)
        {
            error(ENOMEM);
            return;
        }
        (void)disk_usage_->start(target.data());
        state_ = State::measuring;
        return;
    }
    // ---- System info ----
    if (cmd == "uname")
    {
        bool system = false, node = false, release = false, machine = false;
        for (int i = 1; i < argc; ++i)
        {
            if (argv[i][0] != '-' || !argv[i][1])
            {
                usage("uname [-asnrm]");
                return;
            }
            for (const char *f = argv[i] + 1; *f; ++f)
                switch (*f)
                {
                case 'a':
                    system = node = release = machine = true;
                    break;
                case 's':
                    system = true;
                    break;
                case 'n':
                    node = true;
                    break;
                case 'r':
                    release = true;
                    break;
                case 'm':
                    machine = true;
                    break;
                default:
                    usage("uname [-asnrm]");
                    return;
                }
        }
        if (!system && !node && !release && !machine)
            system = true;
        char sysname[64] = "unknown", nodename[256] = "unknown", version[64] = "unknown",
             hardware[64] = "unknown";
#ifdef __PROSPERO__
        // Report what this kernel says; the console's kernel is not a stock FreeBSD release.
        std::size_t length = sizeof(sysname) - 1;
        (void)sysctlbyname("kern.ostype", sysname, &length, nullptr, 0);
        length = sizeof(nodename) - 1;
        (void)sysctlbyname("kern.hostname", nodename, &length, nullptr, 0);
        length = sizeof(version) - 1;
        (void)sysctlbyname("kern.osrelease", version, &length, nullptr, 0);
        length = sizeof(hardware) - 1;
        (void)sysctlbyname("hw.machine", hardware, &length, nullptr, 0);
#else
        struct utsname uts
        {
        };
        if (::uname(&uts) < 0)
        {
            error(errno);
            return;
        }
        std::snprintf(sysname, sizeof sysname, "%s", uts.sysname);
        std::snprintf(nodename, sizeof nodename, "%s", uts.nodename);
        std::snprintf(version, sizeof version, "%s", uts.release);
        std::snprintf(hardware, sizeof hardware, "%s", uts.machine);
#endif
        bool first = true;
        const auto field = [&](bool wanted, const char *value) noexcept
        {
            if (!wanted || !*value)
                return;
            if (!first)
                append(" ");
            first = false;
            append(value);
        };
        field(system, sysname);
        field(node, nodename);
        field(release, version);
        field(machine, hardware);
        append("\n");
        finish();
        return;
    }
    if (cmd == "ps" || cmd == "kill" || cmd == "uptime" || cmd == "free" || cmd == "nproc" ||
        cmd == "sysinfo" || cmd == "sysctl")
    {
        system_command(cmd, argc, argv);
        return;
    }
    if (cmd == "hostname")
    {
        char name[256]{};
#if defined(__PROSPERO__)
        std::size_t size = sizeof name - 1;
        if (sysctlbyname("kern.hostname", name, &size, nullptr, 0) < 0)
        {
            error(errno);
            return;
        }
#else
        if (gethostname(name, sizeof name - 1) < 0)
        {
            error(errno);
            return;
        }
#endif
        append(name);
        append("\n");
        finish();
        return;
    }
    if (cmd == "whoami")
    {
        char msg[32];
        std::snprintf(msg, sizeof msg, "uid %u\n", (unsigned)getuid());
        append(msg);
        finish();
        return;
    }
    if (cmd == "date")
    {
        auto now = std::time(nullptr);
        const struct tm *tmp = std::localtime(&now);
        struct tm tm = tmp ? *tmp : (struct tm){};
        char msg[64];
        std::strftime(msg, sizeof msg, "%a %b %d %H:%M:%S %Z %Y\n", &tm);
        append(msg);
        finish();
        return;
    }
    // ---- Path utilities ----
    if (cmd == "basename")
    {
        if (argc != 2)
        {
            usage("basename PATH");
            return;
        }
        std::string_view p = argv[1];
        while (p.size() > 1 && p.back() == '/')
            p.remove_suffix(1);
        auto last = p.rfind('/');
        if (last != p.npos && p.size() > 1)
            p = p.substr(last + 1);
        append(p);
        append("\n");
        finish();
        return;
    }
    if (cmd == "dirname")
    {
        if (argc != 2)
        {
            usage("dirname PATH");
            return;
        }
        std::string_view p = argv[1];
        while (p.size() > 1 && p.back() == '/')
            p.remove_suffix(1);
        auto last = p.rfind('/');
        if (last == p.npos)
            append(".");
        else if (last == 0)
            append("/");
        else
            append(p.substr(0, last));
        append("\n");
        finish();
        return;
    }
    // ---- Text filters ----
    if (cmd == "grep")
    {
        bool ci = false, inv = false, cnt = false, num = false;
        int arg = 1;
        while (arg < argc && argv[arg][0] == '-' && argv[arg][1])
        {
            if (std::strcmp(argv[arg], "--") == 0)
            {
                ++arg;
                break;
            }
            for (const char *f = argv[arg] + 1; *f; ++f)
            {
                switch (*f)
                {
                case 'i':
                    ci = true;
                    break;
                case 'v':
                    inv = true;
                    break;
                case 'c':
                    cnt = true;
                    break;
                case 'n':
                    num = true;
                    break;
                default:
                    usage("grep [-ivcn] TEXT [FILE]");
                    return;
                }
            }
            ++arg;
        }
        if (arg >= argc)
        {
            usage("grep [-ivcn] TEXT [FILE]");
            return;
        }
        const char *pattern = argv[arg++];
        const auto plen = std::strlen(pattern);
        std::string_view text;
        if (!input_text(arg, argc, argv, text))
            return;
        int line = 0, matches = 0;
        const char *p = text.data(), *end = text.data() + text.size();
        while (p < end)
        {
            const char *eol =
                static_cast<const char *>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (!eol)
                eol = end;
            ++line;
            auto llen = static_cast<std::size_t>(eol - p);
            bool hit = ci ? find_substr_i(p, llen, pattern, plen) != nullptr
                          : find_substr(p, llen, pattern, plen) != nullptr;
            if (inv)
                hit = !hit;
            if (hit)
            {
                ++matches;
                if (!cnt)
                {
                    if (num)
                    {
                        char nb[16];
                        std::snprintf(nb, sizeof nb, "%d:", line);
                        append(nb);
                    }
                    append({p, llen});
                    append("\n");
                }
            }
            p = eol < end ? eol + 1 : end;
        }
        if (cnt)
        {
            char nb[16];
            std::snprintf(nb, sizeof nb, "%d\n", matches);
            append(nb);
        }
        exit_status_ = matches > 0 ? 0 : 1;
        finish();
        return;
    }
    if (cmd == "head")
    {
        unsigned long lines = 10;
        int arg = 1;
        if (!line_option(argc, argv, arg, lines, nullptr))
        {
            usage("head [-n LINES] [FILE]");
            return;
        }
        std::string_view text;
        if (!input_text(arg, argc, argv, text))
            return;
        const char *p = text.data(), *end = text.data() + text.size();
        for (unsigned long count = 0; p < end && count < lines; ++count)
        {
            const char *eol =
                static_cast<const char *>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (!eol)
                eol = end;
            append({p, static_cast<std::size_t>(eol - p)});
            append("\n");
            p = eol < end ? eol + 1 : end;
        }
        finish();
        return;
    }
    if (cmd == "tail")
    {
        unsigned long lines = 10;
        int arg = 1;
        bool from_start = false;
        if (!line_option(argc, argv, arg, lines, &from_start))
        {
            usage("tail [-n [+]LINES] [FILE]");
            return;
        }
        std::string_view text;
        if (!input_text(arg, argc, argv, text))
            return;
        const char *start = text.data(), *end = text.data() + text.size();
        const char *p = start;
        if (from_start)
        {
            for (unsigned long skip = lines > 1 ? lines - 1 : 0; skip && p < end; --skip)
            {
                const char *eol = static_cast<const char *>(
                    std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
                p = eol ? eol + 1 : end;
            }
        }
        else
        {
            // Walk backward so the file's length does not limit the answer.
            p = end;
            if (lines == 0)
                p = end;
            else
            {
                if (p > start && p[-1] == '\n')
                    --p;
                unsigned long seen = 0;
                while (p > start)
                {
                    if (p[-1] == '\n' && ++seen == lines)
                        break;
                    --p;
                }
            }
        }
        while (p < end)
        {
            const char *eol =
                static_cast<const char *>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (!eol)
                eol = end;
            append({p, static_cast<std::size_t>(eol - p)});
            append("\n");
            p = eol < end ? eol + 1 : end;
        }
        finish();
        return;
    }
    if (cmd == "wc")
    {
        bool wl = false, ww = false, wc = false;
        int arg = 1;
        while (arg < argc && argv[arg][0] == '-' && argv[arg][1])
        {
            for (const char *f = argv[arg] + 1; *f; ++f)
            {
                switch (*f)
                {
                case 'l':
                    wl = true;
                    break;
                case 'w':
                    ww = true;
                    break;
                case 'c':
                    wc = true;
                    break;
                default:
                    usage("wc [-lwc] [FILE]");
                    return;
                }
            }
            ++arg;
        }
        if (!wl && !ww && !wc)
        {
            wl = ww = wc = true;
        }
        std::string_view text;
        if (!input_text(arg, argc, argv, text))
            return;
        std::size_t nl = 0, nw = 0;
        bool inword = false;
        for (char ch : text)
        {
            if (ch == '\n')
                ++nl;
            const bool sp = std::isspace(static_cast<unsigned char>(ch));
            if (!sp && !inword)
                ++nw;
            inword = !sp;
        }
        char msg[128];
        int pos = 0;
        if (wl)
            pos += std::snprintf(msg + pos, sizeof msg - static_cast<std::size_t>(pos), "%7zu", nl);
        if (ww)
            pos += std::snprintf(msg + pos, sizeof msg - static_cast<std::size_t>(pos), "%7zu", nw);
        if (wc)
            pos += std::snprintf(msg + pos, sizeof msg - static_cast<std::size_t>(pos), "%7zu",
                                 text.size());
        std::snprintf(msg + pos, sizeof msg - static_cast<std::size_t>(pos), "\n");
        append(msg);
        finish();
        return;
    }
    if (cmd == "sort")
    {
        bool rev = false, numeric = false;
        int arg = 1;
        while (arg < argc && argv[arg][0] == '-' && argv[arg][1])
        {
            for (const char *f = argv[arg] + 1; *f; ++f)
            {
                switch (*f)
                {
                case 'r':
                    rev = true;
                    break;
                case 'n':
                    numeric = true;
                    break;
                default:
                    usage("sort [-rn] [FILE]");
                    return;
                }
            }
            ++arg;
        }
        std::string_view text;
        if (!input_text(arg, argc, argv, text))
            return;
        const char *end = text.data() + text.size();
        std::size_t count = 0;
        for (const char *p = text.data(); p < end; ++count)
        {
            const char *eol =
                static_cast<const char *>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            p = eol ? eol + 1 : end;
        }
        auto *lines = new (std::nothrow) TextLine[count ? count : 1];
        if (!lines)
        {
            io_error_ = ENOMEM;
            finish();
            return;
        }
        std::size_t index = 0;
        for (const char *p = text.data(); p < end; ++index)
        {
            const char *eol =
                static_cast<const char *>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (!eol)
                eol = end;
            lines[index] = {p, static_cast<std::size_t>(eol - p), index};
            p = eol < end ? eol + 1 : end;
        }
        const auto before = [&](const TextLine &a, const TextLine &b) noexcept
        {
            int order = 0;
            if (numeric)
            {
                const auto x = leading_number(a.data, a.size), y = leading_number(b.data, b.size);
                order = x < y ? -1 : x > y ? 1 : 0;
            }
            else
            {
                order = std::memcmp(a.data, b.data, std::min(a.size, b.size));
                if (!order)
                    order = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
            }
            if (rev)
                order = -order;
            return order ? order < 0 : a.order < b.order;
        };
        std::sort(lines, lines + count, before);
        for (std::size_t i = 0; i < count; ++i)
        {
            append({lines[i].data, lines[i].size});
            append("\n");
        }
        delete[] lines;
        finish();
        return;
    }
    if (cmd == "uniq")
    {
        bool count_mode = false;
        int arg = 1;
        if (arg < argc && std::strcmp(argv[arg], "-c") == 0)
        {
            count_mode = true;
            ++arg;
        }
        std::string_view text;
        if (!input_text(arg, argc, argv, text))
            return;
        const char *prev = nullptr;
        std::size_t prev_len = 0;
        int dup_count = 0;
        const auto emit = [&]() noexcept
        {
            if (!prev)
                return;
            if (count_mode)
            {
                char nb[16];
                std::snprintf(nb, sizeof nb, "%7d ", dup_count);
                append(nb);
            }
            append({prev, prev_len});
            append("\n");
        };
        const char *p = text.data(), *end = text.data() + text.size();
        while (p < end)
        {
            const char *eol =
                static_cast<const char *>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (!eol)
                eol = end;
            const auto llen = static_cast<std::size_t>(eol - p);
            if (prev && llen == prev_len && std::memcmp(p, prev, llen) == 0)
                ++dup_count;
            else
            {
                emit();
                prev = p;
                prev_len = llen;
                dup_count = 1;
            }
            p = eol < end ? eol + 1 : end;
        }
        emit();
        finish();
        return;
    }
    if (cmd == "tee")
    {
        bool append_mode = false;
        int arg = 1;
        if (arg < argc && std::strcmp(argv[arg], "-a") == 0)
        {
            append_mode = true;
            ++arg;
        }
        if (arg + 1 != argc)
        {
            usage("tee [-a] FILE");
            return;
        }
        if (!has_stdin())
        {
            usage("tee [-a] FILE (with piped input)");
            return;
        }
        const auto text = pipe_input();
        std::array<char, 1024> target{};
        if (!path(argv[arg], target, false))
        {
            error(errno);
            return;
        }
        int fd = open(target.data(),
                      O_WRONLY | O_CREAT | O_NONBLOCK | (append_mode ? O_APPEND : O_TRUNC), 0666);
        if (fd < 0)
        {
            error(errno);
            return;
        }
        const bool wrote = write_all(fd, text.data(), text.size());
        const int e = errno;
        const int closed = close(fd);
        if (!wrote || closed < 0)
        {
            error(wrote ? errno : e);
            return;
        }
        append(text);
        finish();
        return;
    }
    if (cmd == "xxd")
    {
        unsigned long limit = xxd_default;
        int arg = 1;
        if (arg + 1 < argc && std::strcmp(argv[arg], "-l") == 0)
        {
            if (!parse_count(argv[arg + 1], limit))
            {
                usage("xxd [-l BYTES] [FILE]");
                return;
            }
            arg += 2;
        }
        if (limit > xxd_default)
            limit = xxd_default;
        std::string_view text;
        if (!input_text(arg, argc, argv, text))
            return;
        const auto size = std::min<std::size_t>(text.size(), limit);
        for (std::size_t at = 0; at < size && !io_error_; at += 16)
        {
            char line[96];
            int pos = std::snprintf(line, sizeof line, "%08zx: ", at);
            for (std::size_t i = 0; i < 16; ++i)
            {
                if (at + i < size)
                    pos += std::snprintf(line + pos, sizeof line - static_cast<std::size_t>(pos),
                                         "%02x", static_cast<unsigned char>(text[at + i]));
                else
                    pos += std::snprintf(line + pos, sizeof line - static_cast<std::size_t>(pos),
                                         "  ");
                if (i % 2)
                    line[pos++] = ' ';
            }
            line[pos++] = ' ';
            for (std::size_t i = 0; i < 16 && at + i < size; ++i)
            {
                const auto ch = static_cast<unsigned char>(text[at + i]);
                line[pos++] = ch >= 32 && ch < 127 ? static_cast<char>(ch) : '.';
            }
            line[pos++] = '\n';
            append({line, static_cast<std::size_t>(pos)});
        }
        if (text.size() > size)
            report("xxd: output stopped at the display limit; use -l for less\n");
        finish();
        return;
    }
    if (cmd == "sha256sum")
    {
        if (argc != 2)
        {
            usage("sha256sum FILE");
            return;
        }
        std::array<char, 1024> target{};
        if (!path(argv[1], target, false))
        {
            error(errno);
            return;
        }
        file_ = open(target.data(), O_RDONLY | O_NONBLOCK);
        if (file_ < 0)
        {
            error(errno);
            return;
        }
        struct stat info
        {
        };
        if (fstat(file_, &info) < 0 || !S_ISREG(info.st_mode))
        {
            error(S_ISDIR(info.st_mode) ? EISDIR : EINVAL);
            return;
        }
        sha_state_ = sha_initial;
        sha_total_ = 0;
        argv_ = argv + 1;
        operand_ = 1;
        if (!buffer(slot_file))
        {
            finish();
            return;
        }
        state_ = State::hashing;
        return;
    }
    diagnostic("Unknown command or invalid arguments. Type help.\n", 127);
}
// ---- Command lists ----
// Refuse the whole line before anything runs: a mistake at its end must not leave
// the commands before it half executed.
bool BuiltinShell::check_line() noexcept
{
    static_assert(sizeof(item_) == sizeof(command_));
    Ps5termPipeline trial{};
    const char *s = command_.data();
    bool any = false;
    for (;;)
    {
        char connector = 0;
        const auto length = list_item(s, connector);
        const bool empty = blank(s, length);
        if (connector && empty)
            return false;
        if (!connector && empty && any && s[-1] != ';')
            return false;
        if (!empty)
        {
            std::memcpy(item_.data(), s, length);
            item_[length] = 0;
            if (ps5term_parse_pipeline(item_.data(), &trial) < 0)
                return false;
            any = true;
        }
        if (!connector)
            return true;
        s += length + connector_size(connector);
    }
}
bool BuiltinShell::open_output(const char *name, bool append_mode, int &fd) noexcept
{
    std::array<char, 1024> target{};
    if (!path(name, target, false))
        return false;
    // Nonblocking so a FIFO without a reader fails instead of freezing the app.
    fd = open(target.data(), O_WRONLY | O_CREAT | O_NONBLOCK | (append_mode ? O_APPEND : O_TRUNC),
              0666);
    return fd >= 0;
}
// Set up redirections before running anything, including for an empty "> file".
bool BuiltinShell::prepare_pipeline() noexcept
{
    pipe_stage_ = 0;
    pipe_out_size_ = 0;
    pipe_in_size_ = 0;
    pipe_which_ = false;
    if (pipeline_.redir_in)
    {
        std::array<char, 1024> target{};
        auto *data = pipe_in_ptr();
        const auto n = !data ? -1
                       : !path(pipeline_.redir_in, target, false)
                           ? -1
                           : slurp(target.data(), data, text_limit);
        if (n < 0)
        {
            const int e = errno;
            pipeline_.count = 0;
            command_name_ = pipeline_.redir_in;
            error(e);
            return false;
        }
        pipe_in_size_ = static_cast<std::size_t>(n);
    }
    if (pipeline_.redir_err &&
        !open_output(pipeline_.redir_err, pipeline_.redir_err_append, err_fd_))
    {
        const int e = errno;
        pipeline_.count = 0;
        command_name_ = pipeline_.redir_err;
        error(e);
        return false;
    }
    if (pipeline_.redir_out && !open_output(pipeline_.redir_out, pipeline_.redir_append, redir_fd_))
    {
        const int e = errno;
        pipeline_.count = 0;
        command_name_ = pipeline_.redir_out;
        error(e);
        return false;
    }
    return true;
}
void BuiltinShell::next_item() noexcept
{
    if (!list_checked_)
    {
        list_checked_ = true;
        if (!check_line())
        {
            command_ = {};
            list_position_ = 0;
            pipeline_.count = 0;
            diagnostic(
                "syntax: check quotes and operators; use | ; && || with a command on each side\n",
                2);
            return;
        }
    }
    for (;;)
    {
        const char *s = command_.data() + list_position_;
        char connector = 0;
        const auto length = list_item(s, connector);
        const char previous = list_connector_;
        const bool run = !blank(s, length) && !(previous == '&' && exit_status_ != 0) &&
                         !(previous == '|' && exit_status_ == 0);
        const bool empty = blank(s, length);
        if (run)
        {
            std::memcpy(item_.data(), s, length);
            item_[length] = 0;
            list_position_ += length + connector_size(connector);
            if (connector)
                list_connector_ = connector;
            break;
        }
        // && and || keep the status they were decided on for the next connector.
        list_position_ += length + connector_size(connector);
        if (connector)
            list_connector_ = connector;
        if (!connector || !command_[list_position_])
        {
            (void)empty;
            state_ = State::idle;
            command_name_ = nullptr;
            complete_item();
            return;
        }
    }
    if (!expand_vars(item_.data(), expanded_.data(), expanded_.size()))
    {
        command_ = {};
        list_position_ = 0;
        pipeline_.count = 0;
        diagnostic("syntax: variable expansion error or command too long\n", 2);
        return;
    }
    if (ps5term_parse_pipeline_masked(expanded_.data(), &pipeline_, protection_.data()) < 0)
    {
        command_ = {};
        list_position_ = 0;
        pipeline_.count = 0;
        diagnostic("syntax: check quotes and operators after expanding variables\n", 2);
        return;
    }
    exit_status_ = 0;
    if (platform::FilenameExpansion::needed(pipeline_, expanded_.data(), protection_.data()))
    {
        filename_expansion_.reset(new (std::nothrow) platform::FilenameExpansion(
            pipeline_, expanded_.data(), protection_.data(), cwd_.data()));
        if (!filename_expansion_)
        {
            pipeline_.count = 0;
            diagnostic("wildcard: not enough memory\n", 1);
            return;
        }
        state_ = State::expanding;
        return;
    }
    begin_pipeline();
}
void BuiltinShell::glob_step() noexcept
{
    filename_expansion_->tick();
    if (!filename_expansion_->done())
        return;
    if (const int e = filename_expansion_->error())
    {
        pipeline_.count = 0;
        const char *reason =
            e == ENOTSUP ? "only final-component * and ? are supported; quote literal patterns"
            : e == E2BIG ? "argument, storage or directory scan limit exceeded"
                         : std::strerror(e);
        char message[256];
        std::snprintf(message, sizeof message, "wildcard: %s\n", reason);
        diagnostic(message, 2);
        return;
    }
    begin_pipeline();
}
void BuiltinShell::begin_pipeline() noexcept
{
    if (!prepare_pipeline())
        return;
    if (pipeline_.count == 0 || pipeline_.stages[0].argc == 0)
    {
        finish();
        return;
    }
    piping_ = (pipeline_.count > 1);
    execute_command(pipeline_.stages[0].argc, pipeline_.stages[0].argv);
}
void BuiltinShell::du_step() noexcept
{
    disk_usage_->tick();
    if (!disk_usage_->done())
        return;
    if (const int e = disk_usage_->error())
    {
        char message[1280];
        const char *reason = e == E2BIG   ? "scan entry or hard-link limit exceeded"
                             : e == ELOOP ? "directory depth limit exceeded"
                                          : std::strerror(e);
        std::snprintf(message, sizeof message, "du: %s: %s; no complete summary\n",
                      disk_usage_->error_path(), reason);
        diagnostic(message, 1);
        return;
    }
    const auto blocks = disk_usage_->blocks();
    char count[64];
    if (du_human_)
    {
        double value = static_cast<double>(blocks) * 512.0;
        const char *units = "BKMGTPEZY";
        while (value >= 1024 && units[1])
        {
            value /= 1024;
            ++units;
        }
        if (*units == 'B')
            std::snprintf(count, sizeof count, "%.0fB", value);
        else
            std::snprintf(count, sizeof count, "%.1f%c", value, *units);
    }
    else
        std::snprintf(count, sizeof count, "%llu",
                      static_cast<unsigned long long>(blocks / 2 + blocks % 2));
    append(count);
    append("\t");
    append(du_label_);
    append("\n");
    finish();
}
// ---- Main tick ----
void BuiltinShell::tick() noexcept
{
    if (size_)
        return;
    if (spill_size_)
    {
        const auto count = std::min(spill_size_ - spill_sent_, output_.size());
        std::memcpy(output_.data(), buffers_[slot_spill] + spill_sent_, count);
        size_ = count;
        spill_sent_ += count;
        if (spill_sent_ == spill_size_)
        {
            spill_size_ = spill_sent_ = 0;
            if (spill_then_complete_)
            {
                spill_then_complete_ = false;
                complete_item();
            }
        }
        return;
    }
    if (cancelled_)
    {
        cancelled_ = false;
        finish();
        return;
    }
    switch (state_)
    {
    case State::idle:
        return;
    case State::sleeping:
        if (monotonic_ms() >= sleep_until_)
            finish();
        return;
    case State::program:
        return; // The tab runs it and calls program_finished().
    case State::copying:
        copy_step();
        return;
    case State::hashing:
        hash_step();
        return;
    case State::expanding:
        glob_step();
        return;
    case State::measuring:
        du_step();
        return;
    case State::reading:
    {
        char bytes[2048];
        const auto count = ::read(file_, bytes, sizeof bytes);
        if (count > 0)
        {
            append({bytes, static_cast<std::size_t>(count)});
            if (io_error_)
                finish();
        }
        else if (count == 0)
        {
            close(file_);
            file_ = -1;
            state_ = State::idle;
            cat_next();
        }
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            error(errno);
        return;
    }
    case State::listing:
        for (unsigned i = 0; i < 64; ++i)
        {
            std::string_view name;
            const auto step = directory_.next(name);
            if (step == platform::DirectoryStep::failed)
            {
                const int e = errno;
                ls_flush();
                error(e);
                return;
            }
            if (step == platform::DirectoryStep::end)
            {
                ls_flush();
                directory_.close();
                state_ = State::idle;
                ls_next();
                return;
            }
            if (name.empty())
                continue;
            if (!ls_all_ && name.front() == '.')
                continue;
            auto *names = buffers_[slot_file];
            const auto reserved = (ls_count_ + 1) * sizeof(std::uint32_t);
            if (ls_count_ >= ls_batch || ls_names_ + name.size() + 1 + reserved > text_limit)
            {
                ls_flush();
                if (io_error_)
                {
                    finish();
                    return;
                }
                i = 64;
            }
            std::memcpy(names + ls_names_, name.data(), name.size());
            names[ls_names_ + name.size()] = 0;
            auto *offsets = reinterpret_cast<std::uint32_t *>(names + text_limit) - (ls_count_ + 1);
            *offsets = static_cast<std::uint32_t>(ls_names_);
            ls_names_ += name.size() + 1;
            ++ls_count_;
        }
        return;
    case State::pending:
        next_item();
        return;
    }
}

namespace
{
// Resident memory of this app from its own process record.
bool self_memory(char *out, std::size_t capacity, bool human) noexcept
{
    platform::ProcessRecord records[1];
    auto *all = new (std::nothrow) platform::ProcessRecord[1024];
    if (!all)
        return false;
    platform::ProcessSnapshot snapshot;
    snapshot.records = all;
    snapshot.capacity = 1024;
    platform::snapshot_processes(snapshot);
    bool found = false;
    if (!snapshot.error && snapshot.layout == platform::LayoutCheck::verified)
        for (std::size_t i = 0; i < snapshot.parsed.count; ++i)
            if (all[i].pid == static_cast<int>(getpid()))
            {
                records[0] = all[i];
                found = true;
            }
    delete[] all;
    if (!found || !records[0].rss_pages)
        return false;
    platform::SystemFacts facts;
    platform::collect_system_facts(facts);
    const std::uint64_t bytes = records[0].rss_pages * (facts.page_size ? facts.page_size : 4096);
    if (human)
        platform::format_bytes(bytes, out, capacity);
    else
        std::snprintf(out, capacity, "%llu", static_cast<unsigned long long>(bytes / 1024));
    return true;
}
} // namespace
// ps, kill, uptime, free, nproc, sysinfo, sysctl: read-only sysctls plus kill(2).
void BuiltinShell::system_command(std::string_view cmd, int argc, char **argv) noexcept
{
    char line[256];
    if (cmd == "sysctl")
    {
        // Read-only: sysctl [-n] NAME...; writing kernel values is not offered.
        bool bare = false;
        int first = 1;
        if (first < argc && std::strcmp(argv[first], "-n") == 0)
        {
            bare = true;
            ++first;
        }
        if (first >= argc)
        {
            usage("sysctl [-n] NAME... (read-only)");
            return;
        }
        int status = 0;
        static char value[400];
        for (int i = first; i < argc; ++i)
        {
            if (std::strchr(argv[i], '='))
            {
                report("sysctl: setting values is not supported\n");
                status = 1;
                continue;
            }
            const int code = platform::format_sysctl(argv[i], value, sizeof value);
            if (code)
            {
                std::snprintf(line, sizeof line, "sysctl: %s: %s\n", argv[i], std::strerror(code));
                report(line);
                status = 1;
                continue;
            }
            if (!bare)
            {
                append(argv[i]);
                append(": ");
            }
            append(value);
            append("\n");
        }
        exit_status_ = status;
        finish();
        return;
    }
    if (cmd == "nproc" || cmd == "uptime" || cmd == "free" || cmd == "sysinfo")
    {
        bool human = false;
        for (int i = 1; i < argc; ++i)
        {
            if (cmd == "free" && std::strcmp(argv[i], "-h") == 0)
                human = true;
            else
            {
                usage(cmd == "free" ? "free [-h]" : command_name_);
                return;
            }
        }
        platform::SystemFacts facts;
        platform::collect_system_facts(facts);
        if (cmd == "nproc")
        {
            if (facts.cpus <= 0)
            {
                error(ENOTSUP);
                return;
            }
            std::snprintf(line, sizeof line, "%d\n", facts.cpus);
            append(line);
        }
        else if (cmd == "uptime")
        {
            char up[64];
            platform::format_uptime(facts.boot_seconds, facts.now_seconds, up, sizeof up);
            std::snprintf(line, sizeof line, "up %s\n", up);
            append(line);
        }
        else if (cmd == "free")
        {
            if (!facts.physical_bytes)
            {
                // The PS5 kernel has no memory-total sysctls; show what is knowable.
                char own[32] = "unknown";
                if (self_memory(own, sizeof own, human))
                {
                    std::snprintf(line, sizeof line,
                                  "PS5Term uses %s%s; console memory totals are not available "
                                  "to apps\n",
                                  own, human ? "" : " KiB");
                    append(line);
                }
                else
                    append("free: console memory totals are not available to apps\n");
                finish();
                return;
            }
            const auto show = [&](std::uint64_t value, char *out, std::size_t size)
            {
                if (human)
                    platform::format_bytes(value, out, size);
                else
                    std::snprintf(out, size, "%llu", static_cast<unsigned long long>(value / 1024));
            };
            char total[32], used[32] = "-", available[32] = "-";
            show(facts.physical_bytes, total, sizeof total);
            if (facts.free_known && facts.free_bytes <= facts.physical_bytes)
            {
                show(facts.physical_bytes - facts.free_bytes, used, sizeof used);
                show(facts.free_bytes, available, sizeof available);
            }
            append(human ? "        total     used     free\n"
                         : "        total(KiB) used(KiB) free(KiB)\n");
            std::snprintf(line, sizeof line, human ? "Mem: %9s %8s %8s\n" : "Mem: %13s %9s %9s\n",
                          total, used, available);
            append(line);
        }
        else
        {
            char sdk[48], up[64], memory[48] = "unknown";
            platform::format_sdk_version(facts.sdk_version, sdk, sizeof sdk);
            platform::format_uptime(facts.boot_seconds, facts.now_seconds, up, sizeof up);
            if (facts.physical_bytes)
                platform::format_bytes(facts.physical_bytes, memory, sizeof memory);
            else
            {
                char own[32];
                if (self_memory(own, sizeof own, true))
                    std::snprintf(memory, sizeof memory, "app uses %s", own);
            }
            std::snprintf(line, sizeof line, "Model:     %s (%s)\nCPUs:      %d\nMemory:    %s\n",
                          facts.model, facts.machine, facts.cpus, memory);
            append(line);
            std::snprintf(line, sizeof line, "Kernel:    %s %s\nSDK:       %s\nUptime:    %s\n",
                          facts.ostype, facts.osrelease, sdk, up);
            append(line);
            std::snprintf(line, sizeof line, "Build:     %s\n", facts.version);
            append(line);
            std::snprintf(line, sizeof line, "App:       pid %d, uid %u\n",
                          static_cast<int>(getpid()), static_cast<unsigned>(getuid()));
            append(line);
            auto *records = new (std::nothrow) platform::ProcessRecord[1024];
            const char *visibility = "unavailable (out of memory)";
            if (records)
            {
                platform::ProcessSnapshot snapshot;
                snapshot.records = records;
                snapshot.capacity = 1024;
                platform::snapshot_processes(snapshot);
                if (snapshot.error)
                    visibility = "not permitted";
                else if (snapshot.self_only)
                    visibility = "this app only";
                else if (snapshot.layout != platform::LayoutCheck::verified)
                    visibility = "names only (unrecognised record layout)";
                else
                    visibility = "all processes";
                delete[] records;
            }
            std::snprintf(line, sizeof line,
                          "Processes: %s\nChildren:  not supported (use Settings > Start Linux "
                          "for a full shell)\n",
                          visibility);
            append(line);
        }
        finish();
        return;
    }
    if (cmd == "ps")
    {
        bool full = false;
        for (int i = 1; i < argc; ++i)
        {
            // Every process is always listed; accept the usual spellings of
            // "all" and treat f/u/l as "show more columns".
            const char *flag = argv[i][0] == '-' ? argv[i] + 1 : argv[i];
            bool valid = *flag != 0;
            for (const char *c = flag; *c; ++c)
            {
                if (*c == 'f' || *c == 'u' || *c == 'l')
                    full = true;
                else if (*c != 'e' && *c != 'a' && *c != 'x' && *c != 'A')
                    valid = false;
            }
            if (!valid)
            {
                usage("ps [-f]");
                return;
            }
        }
        auto *records = new (std::nothrow) platform::ProcessRecord[1024];
        if (!records)
        {
            error(ENOMEM);
            return;
        }
        platform::ProcessSnapshot snapshot;
        snapshot.records = records;
        snapshot.capacity = 1024;
        platform::snapshot_processes(snapshot);
        if (snapshot.error)
        {
            delete[] records;
            error(snapshot.error);
            return;
        }
        const bool trusted = snapshot.layout == platform::LayoutCheck::verified;
        if (!trusted)
            append("  PID COMMAND\n");
        else if (full)
            append("  PID  PPID   UID STAT    RSS(K)     TIME COMMAND\n");
        else
            append("  PID STAT     TIME COMMAND\n");
        std::uint64_t page = 4096;
        {
            platform::SystemFacts facts;
            platform::collect_system_facts(facts);
            if (facts.page_size)
                page = facts.page_size;
        }
        for (std::size_t i = 0; i < snapshot.parsed.count; ++i)
        {
            const auto &r = records[i];
            const unsigned long long seconds = r.runtime_us / 1000000;
            char time_text[24];
            std::snprintf(time_text, sizeof time_text, "%llu:%02llu", seconds / 60, seconds % 60);
            if (!trusted)
                std::snprintf(line, sizeof line, "%5d %s\n", r.pid, r.name);
            else if (full)
                std::snprintf(line, sizeof line, "%5d %5d %5u %-4c %9llu %8s %s\n", r.pid, r.ppid,
                              static_cast<unsigned>(r.uid), r.state,
                              static_cast<unsigned long long>(r.rss_pages * page / 1024), time_text,
                              r.name);
            else
                std::snprintf(line, sizeof line, "%5d %-4c %8s %s\n", r.pid, r.state, time_text,
                              r.name);
            append(line);
        }
        if (snapshot.self_only)
            append("ps: the console shows this app only\n");
        if (!trusted)
            append("ps: unrecognised process record layout; showing PID and name only\n");
        if (snapshot.parsed.truncated)
            append("ps: list truncated at 1024 processes\n");
        delete[] records;
        finish();
        return;
    }
    // kill [-s SIG | -SIG] PID... | kill -l
    int signal_number = SIGTERM, first = 1;
    if (argc >= 2 && std::strcmp(argv[1], "-l") == 0)
    {
        if (argc != 2)
        {
            usage("kill -l");
            return;
        }
        char list[256];
        platform::signal_list(list, sizeof list);
        append(list);
        append("\n");
        finish();
        return;
    }
    if (argc >= 3 && std::strcmp(argv[1], "-s") == 0)
    {
        signal_number = platform::parse_signal(argv[2]);
        first = 3;
    }
    else if (argc >= 2 && argv[1][0] == '-' && argv[1][1] && std::strcmp(argv[1], "--") != 0)
    {
        signal_number = platform::parse_signal(argv[1] + 1);
        first = 2;
    }
    if (first < argc && std::strcmp(argv[first], "--") == 0)
        ++first;
    if (signal_number < 0 || first >= argc)
    {
        usage("kill [-s SIG | -SIG] PID... (kill -l lists signals)");
        return;
    }
    const int self = static_cast<int>(getpid());
    int status = 0;
    for (int i = first; i < argc; ++i)
    {
        char *end = nullptr;
        errno = 0;
        const long pid = std::strtol(argv[i], &end, 10);
        if (errno || end == argv[i] || *end || pid > 0x7fffffffL)
        {
            std::snprintf(line, sizeof line, "kill: %s: not a process ID\n", argv[i]);
            report(line);
            status = 1;
            continue;
        }
        switch (platform::kill_refusal(pid, self))
        {
        case platform::KillRefusal::init:
            std::snprintf(line, sizeof line, "kill: %ld: refusing to signal system process\n", pid);
            report(line);
            status = 1;
            continue;
        case platform::KillRefusal::self:
            report("kill: refusing to signal PS5Term itself; close it from the Home screen\n");
            status = 1;
            continue;
        case platform::KillRefusal::none:
            break;
        }
        if (::kill(static_cast<pid_t>(pid), signal_number) < 0)
        {
            std::snprintf(line, sizeof line, "kill: %ld: %s\n", pid, std::strerror(errno));
            report(line);
            status = 1;
        }
    }
    exit_status_ = status;
    finish();
}
} // namespace term
