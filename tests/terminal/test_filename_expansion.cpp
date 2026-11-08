// PS5Term - deterministic wildcard resource boundaries. GPL-3.0-or-later.
#include "platform/filename_expansion.hpp"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
using namespace term::platform;
namespace
{
unsigned calls = 0, open_count = 0, total = 0;
bool opened = false, read_failure = false;
const char *entry = "unmatched";
} // namespace
namespace term::platform
{
DirectoryCursor::~DirectoryCursor()
{
    close();
}
bool DirectoryCursor::open(const char *, bool) noexcept
{
    assert(!opened);
    directory_ = reinterpret_cast<DIR *>(1);
    opened = true;
    ++open_count;
    return true;
}
DirectoryStep DirectoryCursor::next(std::string_view &name) noexcept
{
    assert(opened);
    ++calls;
    if (read_failure)
    {
        errno = EIO;
        return DirectoryStep::failed;
    }
    if (calls > total)
        return DirectoryStep::end;
    name = entry;
    return DirectoryStep::entry;
}
void DirectoryCursor::close() noexcept
{
    directory_ = nullptr;
    opened = false;
}
} // namespace term::platform
void reset(unsigned n, const char *name = "unmatched", bool failure = false)
{
    assert(!opened);
    calls = open_count = 0;
    total = n;
    entry = name;
    read_failure = failure;
}
void run_case(const char *command, int error)
{
    char line[1024];
    std::snprintf(line, sizeof line, "%s", command);
    unsigned char mask[1024]{};
    Ps5termPipeline pipeline{};
    assert(ps5term_parse_pipeline_masked(line, &pipeline, mask) == 0);
    FilenameExpansion glob(pipeline, line, mask, "/fixture");
    for (unsigned i = 0; !glob.done() && i < 10000; ++i)
    {
        const unsigned before = calls;
        glob.tick();
        assert(calls - before <= 32);
    }
    assert(glob.done() && glob.error() == error && !opened);
}
int main()
{
    reset(FilenameExpansion::entry_limit + 1);
    run_case("echo *.log", E2BIG);
    assert(calls == FilenameExpansion::entry_limit + 1);
    reset(10, "x");
    run_case("echo *.log", 0);
    reset(1, "x", true);
    run_case("echo *", EIO);
    reset(100, "x");
    run_case("echo *", E2BIG);
    assert(calls == 31); // 30 matches fit beside the command name.
    reset(1);
    run_case("echo */x > safe", ENOTSUP);
    assert(!open_count);
    reset(1);
    run_case("echo x > *.log", ENOTSUP);
    assert(!open_count);
    // Fill storage before the per-stage argv limit, using long explicit prefixes.
    std::string parent(800, 'p');
    std::string long_name(255, 'x');
    // Long paths across stages hit the aggregate storage cap, independently of
    // each stage's argument cap. Synthetic argv avoids the parser input bound.
    char pattern[1024];
    std::snprintf(pattern, sizeof pattern, "%s/*", parent.c_str());
    unsigned char mask[1024]{};
    Ps5termPipeline p{};
    p.count = 8;
    for (auto &s : p.stages)
    {
        s.argc = 2;
        s.argv[0] = pattern;
        s.argv[1] = pattern;
    }
    reset(20, long_name.c_str());
    // Per-directory fake iteration uses a global cursor. Twenty matches fit;
    // later patterns see no entries, retaining their long literal names.
    {
        FilenameExpansion glob(p, pattern, mask, "/fixture");
        for (int i = 0; !glob.done() && i < 10000; ++i)
            glob.tick();
        assert(glob.done());
        assert(glob.error() == E2BIG);
    }
    assert(!opened);
    // Destruction during scanning owns and closes the cursor.
    reset(1000);
    {
        char line[] = "echo *.log";
        unsigned char protection[sizeof line]{};
        Ps5termPipeline pipeline{};
        assert(!ps5term_parse_pipeline_masked(line, &pipeline, protection));
        FilenameExpansion glob(pipeline, line, protection, "/fixture");
        glob.tick();
        assert(opened && !glob.done());
    }
    assert(!opened);
    std::puts("filename expansion boundary tests passed");
}
