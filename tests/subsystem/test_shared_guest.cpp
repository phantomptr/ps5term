// PS5Term GPL-3.0-or-later. Actual Linux virtio-9p and native file owner.
#include "embedded_linux.h"
#include "platform/linux_shared_folder.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
static std::vector<uint8_t> load(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    assert(f);
    return {std::istreambuf_iterator<char>(f), {}};
}
static void until(Ps5Linux *vm, const char *marker)
{
    std::string out;
    time_t deadline = time(nullptr) + 60;
    while (out.find(marker) == std::string::npos)
    {
        assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING);
        char b[4096];
        size_t n = ps5_linux_read(vm, b, sizeof(b));
        fwrite(b, 1, n, stdout);
        out.append(b, n);
        assert(time(nullptr) < deadline && out.size() < 262144);
    }
}
static void command(Ps5Linux *vm, const char *s)
{
    assert(ps5_linux_write(vm, s, strlen(s)) == strlen(s));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string path = argv[1];
    auto bios = load(path + "/bbl64.bin"), kernel = load(path + "/kernel-riscv64.bin"),
         disk = load(path + "/root-riscv64.bin");
    char temp[] = "/tmp/ps5term-9p-XXXXXX";
    assert(mkdtemp(temp));
    std::string dir = temp;
    {
        std::ofstream f(dir + "/from-native.txt");
        f << "native-value\n";
    }
    // The two folders a guest program must never see, and a tree to walk.
    std::filesystem::create_directories(dir + "/ps5term/ssh");
    std::filesystem::create_directories(dir + "/ps5term/linux");
    std::filesystem::create_directories(dir + "/tree/a/b");
    {
        std::ofstream(dir + "/ps5term/ssh/password") << "secret";
        std::ofstream(dir + "/tree/a/b/leaf") << "leaf\n";
        for (int i = 0; i < 300; i++)
            std::ofstream(dir + "/tree/a/n" + std::to_string(i)) << "x";
    }
    term::platform::LinuxSharedFolder share;
    assert(share.open(dir.c_str()));
    Ps5LinuxImages images{};
    images.bios = bios.data();
    images.bios_size = bios.size();
    images.kernel = kernel.data();
    images.kernel_size = kernel.size();
    images.disk = disk.data();
    images.disk_size = disk.size();
    images.shared_folder = share.device();
    Ps5Linux *vm = ps5_linux_create(160 * 1024 * 1024);
    assert(vm && ps5_linux_start(vm, &images));
    until(vm, " # ");
    command(vm, "grep -q ' /mnt/ps5 ' /proc/mounts && test \"$(cat /mnt/ps5/from-native.txt)\" = "
                "native-value && printf 'SHARE_%s\\n' READ\n");
    until(vm, "SHARE_READ\r\n");
    command(
        vm,
        "echo guest-value >/mnt/ps5/from-guest.txt && echo appended >>/mnt/ps5/from-guest.txt && "
        "test \"$(wc -c </mnt/ps5/from-guest.txt)\" -eq 21 && printf 'SHARE_%s\\n' WRITTEN\n");
    until(vm, "SHARE_WRITTEN\r\n");
    {
        std::ifstream f(dir + "/from-guest.txt");
        std::string s((std::istreambuf_iterator<char>(f)), {});
        assert(s == "guest-value\nappended\n");
    }
    command(vm, "ls /mnt/ps5; if ln -s /etc/passwd /mnt/ps5/escape; then echo BAD; else printf "
                "'SHARE_%s\\n' LINK_DENIED; fi\n");
    until(vm, "SHARE_LINK_DENIED\r\n");
    command(vm, "echo replacement >/mnt/ps5/from-native.txt && printf 'SHARE_%s\\n' TRUNCATED\n");
    until(vm, "SHARE_TRUNCATED\r\n");
    // A file open across rm stays readable and sizable; mv moves and replaces.
    command(vm, "echo a >/mnt/ps5/rm-me && exec 3</mnt/ps5/rm-me && rm /mnt/ps5/rm-me && "
                "test ! -e /mnt/ps5/rm-me && test \"$(wc -c <&3)\" -eq 2 && exec 3<&- && "
                "echo old >/mnt/ps5/moved && echo new >/mnt/ps5/mv-me && "
                "mv /mnt/ps5/mv-me /mnt/ps5/moved && test \"$(cat /mnt/ps5/moved)\" = new && "
                "test ! -e /mnt/ps5/mv-me && printf 'SHARE_%s\\n' REMOVED\n");
    until(vm, "SHARE_REMOVED\r\n");
    // Subfolders, and a renamed directory's contents stay reachable through the new name.
    command(vm,
            "mkdir -p /mnt/ps5/made/deep && echo hi >/mnt/ps5/made/deep/f && "
            "mv /mnt/ps5/made /mnt/ps5/made2 && test \"$(cat /mnt/ps5/made2/deep/f)\" = hi && "
            "cd /mnt/ps5/made2/deep && test \"$(cat f)\" = hi && mv /mnt/ps5/made2 /mnt/ps5/made3 "
            "&& test \"$(cat f)\" = hi && cd / && printf 'SHARE_%s\\n' TREE\n");
    until(vm, "SHARE_TREE\r\n");
    // Past both old exchange limits, through the real client, with exact bytes.
    command(vm, "dd if=/dev/urandom of=/tmp/big bs=1024 count=6144 2>/dev/null && "
                "cp /tmp/big /mnt/ps5/big && cmp /tmp/big /mnt/ps5/big && "
                "printf 'SHARE_%s\\n' LARGE\n");
    until(vm, "SHARE_LARGE\r\n");
    assert(std::filesystem::file_size(dir + "/big") == 6144u * 1024u);
    // find opens every parent and walks 300 names: fids must not run out, and it must count them.
    command(
        vm,
        "test \"$(find /mnt/ps5/tree | wc -l)\" -eq 304 && test \"$(ls /mnt/ps5/tree/a | wc -l)\" "
        "-eq 301 && printf 'SHARE_%s\\n' WALKED\n");
    until(vm, "SHARE_WALKED\r\n");
    // Handles are released: a long create/remove loop must not run out of anything.
    command(vm, "i=0; while [ $i -lt 200 ]; do echo x >/mnt/ps5/loop && rm /mnt/ps5/loop || break; "
                "i=$((i+1)); done; [ $i -eq 200 ] && printf 'SHARE_%s\\n' RELEASED\n");
    until(vm, "SHARE_RELEASED\r\n");
    // rm -r reads and removes in one directory, which skips entries if counting goes wrong.
    command(vm, "rm -r /mnt/ps5/tree && test ! -e /mnt/ps5/tree && printf 'SHARE_%s\\n' RMTREE\n");
    until(vm, "SHARE_RMTREE\r\n");
    // Metadata programs ask for: touch, chmod, chown to root, and df.
    command(vm, "touch /mnt/ps5/moved && chmod 640 /mnt/ps5/moved && chown 0:0 /mnt/ps5/moved && "
                "ls -l /mnt/ps5/moved | grep -q '^-rw-r-----' && df /mnt/ps5 >/dev/null && "
                "if chown 5 /mnt/ps5/moved 2>/dev/null; then echo BAD; else printf 'SHARE_%s\\n' "
                "META; fi\n");
    until(vm, "SHARE_META\r\n");
    // Neither the secret folder nor a walk through it exists for the guest.
    command(vm, "if ls /mnt/ps5/ps5term | grep -q 'ssh\\|linux'; then echo BAD; "
                "elif cat /mnt/ps5/ps5term/ssh/password 2>/dev/null; then echo BAD; "
                "elif echo x 2>/dev/null >/mnt/ps5/ps5term/ssh/new; then echo BAD; "
                "elif mkdir /mnt/ps5/ps5term/ssh 2>/dev/null; then echo BAD; "
                "else printf 'SHARE_%s\\n' HIDDEN; fi\n");
    until(vm, "SHARE_HIDDEN\r\n");
    assert(!std::filesystem::exists(dir + "/rm-me"));
    assert(!std::filesystem::exists(dir + "/mv-me"));
    assert(!std::filesystem::exists(dir + "/loop"));
    assert(!std::filesystem::exists(dir + "/tree"));
    assert(!std::filesystem::exists(dir + "/ps5term/ssh/new"));
    {
        std::ifstream f(dir + "/moved");
        std::string s((std::istreambuf_iterator<char>(f)), {});
        assert(s == "new\n");
    }
    {
        std::ifstream f(dir + "/made3/deep/f");
        std::string s((std::istreambuf_iterator<char>(f)), {});
        assert(s == "hi\n");
    }
    ps5_linux_stop(vm);
    share.close();
    assert(share.open(dir.c_str()));
    images.shared_folder = share.device();
    assert(ps5_linux_start(vm, &images));
    until(vm, " # ");
    command(vm, "test \"$(cat /mnt/ps5/from-native.txt)\" = replacement && test -f "
                "/mnt/ps5/from-guest.txt && test ! -e /mnt/ps5/rm-me && test -f /mnt/ps5/moved && "
                "printf 'SHARE_%s\\n' REOPENED\n");
    until(vm, "SHARE_REOPENED\r\n");
    ps5_linux_stop(vm);
    images.shared_folder.read = nullptr;
    assert(!ps5_linux_start(vm, &images) && ps5_linux_error(vm) == PS5_LINUX_ARGUMENT);
    ps5_linux_destroy(vm);
    share.close();
    std::filesystem::remove_all(dir);
    puts("PASS: actual Linux 9p mount of a host tree: import, export, subfolders, renamed trees, "
         "large "
         "files, find, rm -r, metadata, hidden folders and reopen");
}
