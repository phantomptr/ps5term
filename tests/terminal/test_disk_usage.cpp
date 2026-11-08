// PS5Term - traversal bounds and failure cleanup, with a deterministic cursor.
// GPL-3.0-or-later.
#include "platform/disk_usage.hpp"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <map>
#include <string>
#include <limits>
using term::platform::DirectoryCursor;
using term::platform::DirectoryStep;
using term::platform::DiskUsage;
namespace
{
enum class Scenario
{
    entries,
    links,
    duplicate,
    depth,
    read_error,
    swapped,
    overflow
};
Scenario scenario;
std::map<const DirectoryCursor *, unsigned> cursors;
std::string name;
unsigned metadata_calls = 0;
int inspect(const char *path, struct stat *st)
{
    ++metadata_calls;
    *st = {};
    const bool root = std::string_view(path) == "/scan";
    st->st_mode =
        root || scenario == Scenario::depth || scenario == Scenario::swapped ? S_IFDIR : S_IFREG;
    st->st_blocks = 3;
    st->st_dev = 1;
    st->st_ino = scenario == Scenario::duplicate ? 2 : metadata_calls;
    st->st_nlink = scenario == Scenario::links || scenario == Scenario::duplicate ? 2 : 1;
    if (scenario == Scenario::overflow)
        st->st_blocks = std::numeric_limits<decltype(st->st_blocks)>::max();
    return 0;
}
void complete(DiskUsage &scan)
{
    for (unsigned ticks = 0; !scan.done() && ticks < 10000; ++ticks)
    {
        const auto before = metadata_calls;
        scan.tick();
        assert(metadata_calls - before <= 32);
    }
    assert(scan.done() && cursors.empty());
}
} // namespace
namespace term::platform
{
DirectoryCursor::~DirectoryCursor()
{
    close();
}
void DirectoryCursor::close() noexcept
{
    cursors.erase(this);
}
bool DirectoryCursor::open(const char *path, bool no_follow) noexcept
{
    assert(no_follow);
    assert(directory_ == nullptr);
    if (scenario == Scenario::swapped && std::string_view(path) != "/scan")
    {
        errno = ELOOP;
        return false;
    }
    cursors[this] = 0;
    return true;
}
DirectoryStep DirectoryCursor::next(std::string_view &out) noexcept
{
    if (scenario == Scenario::read_error)
    {
        errno = EIO;
        return DirectoryStep::failed;
    }
    unsigned &position = cursors.at(this);
    const unsigned count = scenario == Scenario::entries     ? DiskUsage::entry_limit + 1
                           : scenario == Scenario::links     ? DiskUsage::link_limit + 1
                           : scenario == Scenario::duplicate ? 100
                           : scenario == Scenario::overflow  ? 3
                                                             : 1;
    if (position == count)
        return DirectoryStep::end;
    name = "n" + std::to_string(position++);
    out = name;
    return DirectoryStep::entry;
}
} // namespace term::platform
int main()
{
    for (auto s : {Scenario::entries, Scenario::links, Scenario::depth, Scenario::read_error,
                   Scenario::swapped, Scenario::overflow})
    {
        scenario = s;
        metadata_calls = 0;
        DiskUsage scan(inspect);
        assert(scan.start("/scan"));
        complete(scan);
        const int expected = s == Scenario::entries || s == Scenario::links   ? E2BIG
                             : s == Scenario::depth || s == Scenario::swapped ? ELOOP
                             : s == Scenario::overflow                        ? EOVERFLOW
                                                                              : EIO;
        assert(scan.error() == expected);
        assert(std::string_view(scan.error_path()).starts_with("/scan"));
    }
    scenario = Scenario::duplicate;
    {
        DiskUsage scan(inspect);
        assert(scan.start("/scan"));
        complete(scan);
        assert(scan.error() == 0 && scan.blocks() == 6);
        // Restart and destruction must both release a live traversal.
        assert(scan.start("/scan") && !cursors.empty());
        assert(scan.start("/scan") && cursors.size() == 1);
    }
    assert(cursors.empty());
    {
        DiskUsage scan(inspect);
        assert(!scan.start("") && scan.error() == EINVAL);
        const std::string long_path(1024, 'p');
        assert(!scan.start(long_path.c_str()) && scan.error() == ENAMETOOLONG);
    }
    std::puts("disk usage limits, bounded ticks and cleanup passed");
}
