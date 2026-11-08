// PS5Term - owned, bounded line interpreter. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "platform/directory.hpp"
#include "platform/disk_usage.hpp"
#include "platform/filename_expansion.hpp"
#include "../backend/ps5term_pipeline.h"
namespace term
{
class BuiltinShell
{
  public:
    explicit BuiltinShell(std::string_view directory = "/data", bool interactive = true) noexcept;
    ~BuiltinShell();
    BuiltinShell(const BuiltinShell &) = delete;
    BuiltinShell &operator=(const BuiltinShell &) = delete;
    bool submit(std::string_view command) noexcept;
    void reset(std::string_view directory = "/data") noexcept;
    void tick() noexcept;
    bool interrupt() noexcept;
    std::size_t read(char *destination, std::size_t capacity) noexcept;
    std::size_t read_error(char *destination, std::size_t capacity) noexcept;
    int exit_status() const noexcept
    {
        return exit_status_;
    }
    bool busy() const noexcept;
    // Largest text a filter, a pipe stage or one command's terminal output can hold.
    static constexpr std::size_t text_limit = 512 * 1024;

  private:
    enum class State
    {
        idle,
        pending,
        listing,
        reading,
        copying,
        hashing,
        sleeping,
        measuring,
        expanding
    };
    // Allocated when first needed and released when the command line ends.
    enum Slot
    {
        slot_pipe_a,
        slot_pipe_b,
        slot_file,
        slot_spill,
        slot_count
    };
    State state_ = State::idle;
    std::array<char, 1024> cwd_{}, previous_cwd_{};
    std::array<char, 513> command_{};
    std::size_t list_position_ = 0;
    char list_connector_ = ';';
    bool list_checked_ = false;
    std::array<char, 4096> output_{};
    std::size_t size_ = 0;
    std::array<char, 512> errors_{};
    std::size_t error_size_ = 0;
    bool interactive_ = true;
    int exit_status_ = 0;
    platform::DirectoryCursor directory_{};
    int file_ = -1, copy_out_ = -1;
    bool cancelled_ = false;
    std::array<char *, slot_count> buffers_{};
    std::size_t spill_size_ = 0, spill_sent_ = 0;
    bool spill_then_complete_ = false;
    // Pipeline
    Ps5termPipeline pipeline_{};
    int pipe_stage_ = 0;
    std::size_t pipe_in_size_ = 0;
    std::size_t pipe_out_size_ = 0;
    bool pipe_which_ = false;
    bool piping_ = false;
    int redir_fd_ = -1, err_fd_ = -1;
    int io_error_ = 0;
    const char *command_name_ = nullptr;
    // Multi-operand commands
    int argc_ = 0, operand_ = 0;
    char **argv_ = nullptr;
    bool wrote_ = false;
    char last_byte_ = 0;
    // ls
    bool ls_all_ = false, ls_long_ = false, ls_human_ = false, ls_header_ = false;
    std::size_t ls_names_ = 0, ls_count_ = 0;
    std::array<char, 1024> ls_directory_{};
    // sha256sum
    std::array<std::uint32_t, 8> sha_state_{};
    std::array<unsigned char, 64> sha_block_{};
    std::uint64_t sha_total_ = 0;
    // sleep
    std::uint64_t sleep_until_ = 0;
    std::unique_ptr<platform::DiskUsage> disk_usage_;
    const char *du_label_ = nullptr;
    bool du_human_ = false;
    std::unique_ptr<platform::FilenameExpansion> filename_expansion_;
    std::array<unsigned char, 1024> protection_{};
    // Environment variables
    static constexpr int max_env_ = 24;
    struct EnvEntry
    {
        char key[32]{};
        char value[128]{};
    };
    std::array<EnvEntry, max_env_> env_{};
    int env_count_ = 0;
    // Variable expansion
    std::array<char, 513> item_{};
    std::array<char, 1024> expanded_{};
    // Core helpers
    bool path(std::string_view value, std::array<char, 1024> &result,
              bool normalize = true) const noexcept;
    void append(std::string_view value) noexcept;
    void put_terminal(std::string_view value) noexcept;
    void report(std::string_view message) noexcept;
    void finish() noexcept;
    void complete_item() noexcept;
    void error(int code) noexcept;
    void diagnostic(std::string_view message, int status) noexcept;
    void usage(const char *text) noexcept;
    void close_resources() noexcept;
    void release_buffers() noexcept;
    char *buffer(Slot) noexcept;
    bool check_line() noexcept;
    void next_item() noexcept;
    bool open_output(const char *name, bool append_mode, int &fd) noexcept;
    bool prepare_pipeline() noexcept;
    // Pipeline helpers
    char *pipe_in_ptr() noexcept
    {
        return buffer(pipe_which_ ? slot_pipe_b : slot_pipe_a);
    }
    char *pipe_out_ptr() noexcept
    {
        return buffer(pipe_which_ ? slot_pipe_a : slot_pipe_b);
    }
    std::string_view pipe_input() noexcept;
    bool has_stdin() const noexcept
    {
        return pipe_stage_ > 0 || pipeline_.redir_in;
    }
    bool input_text(int arg, int argc, char **argv, std::string_view &text) noexcept;
    void execute_command(int argc, char **argv) noexcept;
    void system_command(std::string_view cmd, int argc, char **argv) noexcept;
    void advance_pipe() noexcept;
    // Commands with state across ticks
    void cat_next() noexcept;
    void ls_next() noexcept;
    void ls_flush() noexcept;
    void ls_print(const char *directory, const char *name) noexcept;
    void copy_step() noexcept;
    void hash_step() noexcept;
    void du_step() noexcept;
    void glob_step() noexcept;
    void begin_pipeline() noexcept;
    void sha_update(const unsigned char *data, std::size_t size) noexcept;
    bool start_copy(const char *source, const char *destination) noexcept;
    // Environment helpers
    const char *env_get(std::string_view key) const noexcept;
    bool env_set(std::string_view key, std::string_view value) noexcept;
    void env_unset(std::string_view key) noexcept;
    bool expand_vars(const char *in, char *out, std::size_t cap) const noexcept;
};
} // namespace term
