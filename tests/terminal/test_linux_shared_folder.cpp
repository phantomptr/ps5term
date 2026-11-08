// PS5Term GPL-3.0-or-later.
#include "platform/linux_shared_folder.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using term::platform::LinuxSharedFolder;
namespace fs = std::filesystem;
static void put(const fs::path &p, const std::string &s)
{
    std::ofstream(p, std::ios::binary) << s;
}
static std::string get(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
static std::set<std::string> names(const Ps5LinuxShare &api, unsigned key, const char *dir)
{
    std::set<std::string> out;
    char name[PS5_SHARE_NAME];
    for (std::uint64_t cookie = 0;; cookie++)
    {
        const int r = api.list(api.context, key, dir, cookie, name, sizeof(name));
        assert(r >= 0);
        if (!r)
            break;
        out.insert(name);
    }
    api.list_close(api.context, key);
    return out;
}
int main()
{
    char temp[] = "/tmp/ps5term-host-XXXXXX";
    assert(mkdtemp(temp));
    const fs::path root(temp);
    put(root / "input.txt", "native");
    fs::create_directories(root / "dir/sub");
    put(root / "dir/sub/deep.txt", "deep");
    assert(::symlink("/etc/passwd", (root / "link").c_str()) == 0);
    assert(::mkfifo((root / "pipe").c_str(), 0600) == 0);
    // These hold the SSH password and the disk Linux runs from. The second spelling is how
    // /data also appears when the whole console is mounted.
    fs::create_directories(root / "ps5term/ssh");
    fs::create_directories(root / "ps5term/linux");
    fs::create_directories(root / "user/data/ps5term/ssh");
    fs::create_directories(root / "ps5term/shared");
    put(root / "ps5term/ssh/password", "secret");
    put(root / "ps5term/linux/data.ext2", "disk");
    put(root / "user/data/ps5term/ssh/password", "secret");

    LinuxSharedFolder folder;
    assert(!folder.open((root / "absent").c_str()) && folder.error() == ENOENT);
    assert(!folder.open((root / "input.txt").c_str()) && folder.error() == ENOTDIR);
    assert(folder.open(temp));
    auto api = folder.device();
    auto *c = api.context;
    Ps5ShareStat st;

    // Metadata, including what must not be opened.
    assert(api.stat(c, "", &st) == 0 && (st.mode & 0170000) == 0040000);
    assert(api.stat(c, "dir/sub/deep.txt", &st) == 0 && st.size == 4);
    assert((st.mode & 0170000) == 0100000 && st.mtime_sec > 0);
    assert(api.stat(c, "link", &st) == 0 && (st.mode & 0170000) == 0120000);
    assert(api.stat(c, "absent", &st) == -2);
    assert(api.stat(c, "input.txt/x", &st) == -20);
    char text[64];
    assert(api.readlink(c, "link", text, sizeof(text)) == 0 && !std::strcmp(text, "/etc/passwd"));
    assert(api.readlink(c, "input.txt", text, sizeof(text)) == -22);
    assert(api.open(c, "pipe", PS5_SHARE_READ, 0) == -1); // Would otherwise block the worker.
    assert(api.open(c, "dir", PS5_SHARE_READ, 0) == -21);
    assert(api.open(c, "link", PS5_SHARE_READ, 0) == -1);

    // The protected folders are absent by every operation and every spelling.
    for (const char *secret : {"ps5term/ssh", "ps5term/ssh/password", "ps5term/linux/data.ext2",
                               "user/data/ps5term/ssh/password"})
    {
        assert(api.stat(c, secret, &st) == -2);
        assert(api.open(c, secret, PS5_SHARE_READ, 0) == -2);
        assert(api.open(c, secret, PS5_SHARE_WRITE | PS5_SHARE_CREATE, 0600) == -13);
        assert(api.remove(c, secret) == -2);
        assert(api.rename(c, secret, "stolen") == -13);
        assert(api.rename(c, "input.txt", secret) == -13);
        assert(api.resize(c, 0, secret, 0) == -2);
        assert(api.chmod(c, secret, 0644) == -2);
        assert(api.readlink(c, secret, text, sizeof(text)) == -2);
        assert(api.times(c, secret, 0, 0, 0, 0, 1) == -2);
    }
    assert(api.mkdir(c, "ps5term/ssh/x", 0700) == -13);
    // Moving the enclosing folder would rename the secrets out of the protected pair.
    for (const char *parent : {"ps5term", "ps5term/", "user/data/ps5term"})
    {
        assert(api.rename(c, parent, "p") == -13);
        assert(api.remove(c, parent) == -13);
    }
    assert(api.rename(c, "input.txt", "ps5term") == -13);
    assert(get(root / "ps5term/ssh/password") == "secret");
    // chmod must not follow a symlink out of the share.
    assert(api.chmod(c, "link", 0777) == -1);
    assert(get(root / "ps5term/ssh/password") == "secret");
    assert((names(api, 1, "ps5term") == std::set<std::string>{"shared"}));
    char name[PS5_SHARE_NAME];
    assert(api.list(c, 1, "ps5term/ssh", 0, name, sizeof(name)) == -2);
    assert(api.list(c, 1, "ps5term/ssh", 0, name, 8) == -22);

    // Listing has no "." or "..", repeats an entry the guest could not fit, and can restart.
    assert((names(api, 2, "") ==
            std::set<std::string>{"dir", "input.txt", "link", "pipe", "ps5term", "user"}));
    char first[PS5_SHARE_NAME], again[PS5_SHARE_NAME];
    assert(api.list(c, 3, "", 0, first, sizeof(first)) == 1);
    assert(api.list(c, 3, "", 0, again, sizeof(again)) == 1 && !std::strcmp(first, again));
    assert(api.list(c, 3, "", 1, again, sizeof(again)) == 1 && std::strcmp(first, again));
    assert(api.list(c, 3, "", 0, again, sizeof(again)) == 1 && !std::strcmp(first, again));
    api.list_close(c, 3);
    assert(api.list(c, 4, "input.txt", 0, name, sizeof(name)) == -20);

    // More readers than slots: the idle one is evicted, and counts its way back.
    for (unsigned key = 10; key < 10 + LinuxSharedFolder::directory_readers + 2; key++)
        assert(api.list(c, key, "", 0, first, sizeof(first)) == 1);
    assert(api.list(c, 10, "", 1, again, sizeof(again)) == 1 && std::strcmp(first, again));
    for (unsigned key = 10; key < 10 + LinuxSharedFolder::directory_readers + 2; key++)
        api.list_close(c, key);

    // Removing entries while reading one directory sees each exactly once.
    fs::create_directories(root / "many");
    for (unsigned i = 0; i < 300; i++)
        put(root / "many" / ("f" + std::to_string(i)), "x");
    unsigned seen = 0;
    for (std::uint64_t cookie = 0;; cookie++)
    {
        const int r = api.list(c, 5, "many", cookie, name, sizeof(name));
        assert(r >= 0);
        if (!r)
            break;
        assert(api.remove(c, ("many/" + std::string(name)).c_str()) == 0);
        seen++;
    }
    api.list_close(c, 5);
    assert(seen == 300 && fs::is_empty(root / "many"));
    assert(api.remove(c, "many") == 0 && !fs::exists(root / "many"));

    // Files: no size or count limit from the share itself, and subfolders.
    assert(api.open(c, "new/file", PS5_SHARE_WRITE | PS5_SHARE_CREATE, 0600) == -2);
    assert(api.mkdir(c, "new", 0755) == 0);
    assert(api.mkdir(c, "new", 0755) == -17);
    const int out =
        api.open(c, "new/file", PS5_SHARE_READ | PS5_SHARE_WRITE | PS5_SHARE_CREATE, 0640);
    assert(out > 0);
    assert(api.open(c, "new/file", PS5_SHARE_WRITE | PS5_SHARE_CREATE | PS5_SHARE_EXCLUSIVE,
                    0640) == -17);
    assert(api.fstat(c, static_cast<unsigned>(out), &st) == 0 && (st.mode & 0777) == 0640);
    assert(api.write(c, static_cast<unsigned>(out), 0, "guest", 5) == 5);
    // Past both of the old exchange folder's limits, sparsely.
    const std::uint64_t large = 5ull * 1024 * 1024 * 1024;
    assert(api.write(c, static_cast<unsigned>(out), large, "!", 1) == 1);
    assert(api.fstat(c, static_cast<unsigned>(out), &st) == 0 && st.size == large + 1);
    char data[64];
    assert(api.read(c, static_cast<unsigned>(out), large, data, sizeof(data)) == 1 &&
           data[0] == '!');
    assert(api.read(c, static_cast<unsigned>(out), large + 10, data, sizeof(data)) == 0);
    assert(api.resize(c, static_cast<unsigned>(out), "", 2) == 0);
    assert(api.read(c, static_cast<unsigned>(out), 0, data, sizeof(data)) == 2 && data[1] == 'u');
    assert(api.read(c, static_cast<unsigned>(out), 0, data, PS5_SHARE_IO + 1) == -22);
    assert(api.write(c, static_cast<unsigned>(out), 0, data, PS5_SHARE_IO + 1) == -22);
    // A handle survives its name being removed or replaced.
    assert(api.rename(c, "new/file", "new/moved") == 0);
    assert(get(root / "new/moved") == "gu");
    assert(api.remove(c, "new/moved") == 0 && !fs::exists(root / "new/moved"));
    assert(api.read(c, static_cast<unsigned>(out), 0, data, 2) == 2 && data[0] == 'g');
    api.close(c, static_cast<unsigned>(out));
    assert(api.read(c, static_cast<unsigned>(out), 0, data, 2) == -9);
    api.close(c, static_cast<unsigned>(out));
    assert(api.remove(c, "new") == 0);
    // Errors the guest must see as Linux numbers.
    assert(api.remove(c, "dir") == -39);
    assert(api.remove(c, "") == -2);
    assert(api.rename(c, "", "x") == -13);
    assert(api.rename(c, "absent", "x") == -2);
    assert(api.rename(c, "input.txt", "dir/sub") == -21);
    assert(api.stat(c, "/etc/passwd", &st) == -2);
    assert(api.open(c, "/etc/passwd", PS5_SHARE_READ, 0) == -2);

    // Without a handle: resize, mode and time by path, and refusal for what is not a file.
    assert(api.resize(c, 0, "input.txt", 3) == 0 && get(root / "input.txt") == "nat");
    assert(api.resize(c, 0, "dir", 3) == -21);
    assert(api.resize(c, 0, "pipe", 3) == -1);
    assert(api.chmod(c, "input.txt", 0604) == 0);
    assert(api.stat(c, "input.txt", &st) == 0 && (st.mode & 0777) == 0604);
    assert(api.times(c, "input.txt", 1000, 0, 2000, 0, 0) == 0);
    assert(api.stat(c, "input.txt", &st) == 0 && st.mtime_sec == 2000);
    assert(api.times(c, "input.txt", 0, 0, 0, 0, 1) == 0);
    assert(api.stat(c, "input.txt", &st) == 0 && st.mtime_sec > 2000);
    assert(api.times(c, "pipe", 0, 0, 0, 0, 1) == 0); // Never opened.
    assert(api.times(c, "dir", 0, 0, 0, 0, 1) == 0);

    // The open-file table is bounded and recovers.
    std::set<int> held;
    for (std::size_t i = 0; i < LinuxSharedFolder::open_files; i++)
    {
        const int h = api.open(c, "input.txt", PS5_SHARE_READ, 0);
        assert(h > 0);
        held.insert(h);
    }
    assert(held.size() == LinuxSharedFolder::open_files);
    assert(api.open(c, "input.txt", PS5_SHARE_READ, 0) == -23);
    assert(api.resize(c, 0, "input.txt", 1) == -23);
    for (const int h : held)
        api.close(c, static_cast<unsigned>(h));
    assert(api.open(c, "input.txt", PS5_SHARE_READ, 0) > 0); // Left open: close() owns it.

    Ps5ShareSpace space{};
    assert(api.space(c, &space) == 0 && space.block_size && space.blocks);

    folder.close();
    assert(api.stat(c, "", &st) == -2);
    assert(api.open(c, "input.txt", PS5_SHARE_READ, 0) == -2);
    assert(api.space(c, &space) == -5);

    // A root given with a trailing slash joins correctly: this is how "/" is opened.
    const std::string slashed = std::string(temp) + "/";
    assert(folder.open(slashed.c_str()));
    assert(folder.device().stat(c, "input.txt", &st) == 0);
    folder.close();
    assert(folder.open("/"));
    assert(folder.device().stat(c, "", &st) == 0 && (st.mode & 0170000) == 0040000);
    assert(folder.device().stat(c, temp + 1, &st) == 0);
    assert(folder.device().space(c, &space) == 0);
    folder.close();
    fs::remove_all(root);
    puts("Linux host folder tests passed");
}
