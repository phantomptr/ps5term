// PS5Term - bounded native filename expansion. GPL-3.0-or-later.
#pragma once
#include "directory.hpp"
#include "../../backend/ps5term_pipeline.h"
#include <array>
#include <cstddef>
#include <string_view>
namespace term::platform
{
class FilenameExpansion
{
  public:
    static bool needed(const Ps5termPipeline &, const char *line,
                       const unsigned char *protection) noexcept;
    FilenameExpansion(Ps5termPipeline &, const char *line, const unsigned char *protection,
                      const char *cwd) noexcept;
    void tick() noexcept;
    bool done() const noexcept
    {
        return done_;
    }
    int error() const noexcept
    {
        return error_;
    }
    static constexpr std::size_t entry_limit = 100000, storage_limit = 32768;

  private:
    Ps5termPipeline &pipeline_;
    const char *line_, *cwd_;
    const unsigned char *protection_;
    std::array<Ps5termPipeStage, PS5TERM_PIPE_STAGES> stages_{};
    std::array<char, storage_limit> storage_{};
    DirectoryCursor directory_;
    std::size_t used_ = 0, visited_ = 0, prefix_ = 0;
    int stage_ = 0, arg_ = 0, match_start_ = 0, error_ = 0;
    const char *pattern_ = nullptr;
    bool done_ = false;
    bool active(const char *, std::size_t) const noexcept;
    bool put(std::string_view, std::string_view = {}) noexcept;
    bool matches(std::string_view) const noexcept;
    void fail(int) noexcept;
    void finish_pattern() noexcept;
};
} // namespace term::platform
