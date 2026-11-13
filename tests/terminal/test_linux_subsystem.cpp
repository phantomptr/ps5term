// PS5Term - GPL-3.0-or-later. Loader ownership and failure regressions.
#include "platform/linux_subsystem.hpp"
#include "linux_session.h"
#if defined(PS5TERM_LINUX_NETWORK)
#include "net/nat.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
struct Ps5LinuxNat
{
};
static int network_live, network_state = 1;
static bool fail_network;
static uint32_t network_dns;
extern "C"
{
    Ps5LinuxNat *ps5_linux_nat_create(const Ps5LinuxNatConfig *c)
    {
        assert(c && c->dns_port == 53 && !c->allow_host_loopback && c->fail_after == SIZE_MAX);
        network_dns = c->dns_ipv4;
        if (fail_network)
            return nullptr;
        ++network_live;
        return new Ps5LinuxNat;
    }
    Ps5LinuxNetwork ps5_linux_nat_device(Ps5LinuxNat *n)
    {
        Ps5LinuxNetwork d{};
        d.context = n;
        return d;
    }
    void ps5_linux_nat_destroy(Ps5LinuxNat *n)
    {
        if (n)
        {
            --network_live;
            delete n;
        }
    }
}
#endif
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <vector>
#include <unistd.h>
struct Ps5LinuxSession
{
    const unsigned char *bios;
    Ps5LinuxDisk disk;
    Ps5LinuxShare shared;
    Ps5LinuxNetwork network;
};
static int live, destroyed;
static bool fail_start, failed_worker, fail_stop;
[[maybe_unused]] static bool share_required = true;
extern "C"
{
    Ps5LinuxSession *ps5_linux_session_create(size_t)
    {
        ++live;
        return new Ps5LinuxSession{};
    }
    void ps5_linux_session_destroy(Ps5LinuxSession *s)
    {
        assert(s->bios == nullptr || s->bios[0] == 1);
#if defined(PS5TERM_LINUX_PERSISTENCE)
        if (s->disk.sectors)
        {
            unsigned char block[512];
            assert(s->disk.read(s->disk.context, 0, block, 1) == 0);
        }
#endif
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
        Ps5ShareStat bytes{};
        assert(!share_required ||
               (s->shared.context && s->shared.stat(s->shared.context, "", &bytes) == 0));
#endif
#if defined(PS5TERM_LINUX_NETWORK)
        assert(!s->network.context || network_live == 1);
#endif
        --live;
        ++destroyed;
        delete s;
    }
    int ps5_linux_session_start(Ps5LinuxSession *s, const Ps5LinuxImages *i)
    {
        s->bios = i->bios;
        s->disk = i->data_disk;
        s->network = i->network;
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
        s->shared = i->shared_folder;
        if (!share_required)
        {
            assert(!s->shared.context);
            return !fail_start;
        }
        assert(s->shared.context);
        const int file = s->shared.open(s->shared.context, "mock-file",
                                        PS5_SHARE_WRITE | PS5_SHARE_CREATE, 0600);
        assert(file > 0);
        s->shared.close(s->shared.context, static_cast<unsigned>(file));
#endif
        return !fail_start;
    }
    int ps5_linux_session_stop(Ps5LinuxSession *)
    {
        return !fail_stop;
    }
    int ps5_linux_session_resize(Ps5LinuxSession *, unsigned, unsigned)
    {
        return 1;
    }
    size_t ps5_linux_session_read(Ps5LinuxSession *, void *, size_t)
    {
        return 0;
    }
    size_t ps5_linux_session_write(Ps5LinuxSession *, const void *, size_t n)
    {
        return n;
    }
    size_t ps5_linux_session_agent_write(Ps5LinuxSession *, const void *, size_t n)
    {
        return n;
    }
    size_t ps5_linux_session_agent_read(Ps5LinuxSession *, void *, size_t)
    {
        return 0;
    }
    Ps5LinuxSessionStatus ps5_linux_session_status(Ps5LinuxSession *session)
    {
        (void)session;
        Ps5LinuxSessionStatus s{};
        s.state = failed_worker ? PS5_SESSION_FAILED : PS5_SESSION_RUNNING;
#if defined(PS5TERM_LINUX_NETWORK)
        s.network_state = session->network.context ? network_state : 0;
#endif
        return s;
    }
}
static void put(const std::string &p, const std::vector<unsigned char> &v)
{
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(v.data()), v.size());
}
int main()
{
    char dir[] = "/tmp/ps5term-linux-XXXXXX";
    assert(mkdtemp(dir));
    std::string base = std::string(dir) + "/";
    std::vector<unsigned char> bios(64, 1), kernel(2 * 1024 * 1024), disk(2 * 1024 * 1024);
    kernel[10] = 0x20;
    kernel[16] = 64;
    kernel[56] = 'R';
    kernel[57] = 'S';
    kernel[58] = 'C';
    kernel[59] = 5;
    disk[1080] = 0x53;
    disk[1081] = 0xef;
    put(base + "bbl64.bin", bios);
    put(base + "kernel-riscv64.bin", kernel);
    put(base + "root-riscv64.bin", disk);
#if defined(PS5TERM_LINUX_PERSISTENCE)
    std::vector<unsigned char> seed(16 * 1024 * 1024);
    seed[1028 + 1] = 0x40; // 16384 blocks, 16 MiB.
    seed[1080] = 0x53;
    seed[1081] = 0xef;
    put(base + "data-seed.ext2", seed);
#endif
    std::string data_directory = base + "persist", shared_directory = base + "shared",
                console_directory = base + "console";
    mkdir(shared_directory.c_str(), 0700);
    mkdir(console_directory.c_str(), 0700);
    {
        // The choice defaults to /data, is saved, survives a new object, and a damaged file
        // never widens access.
        term::platform::LinuxSubsystem choice(dir, data_directory.c_str(), shared_directory.c_str(),
                                              console_directory.c_str());
        assert(!choice.mounts_whole_console());
        assert(choice.set_mounts_whole_console(true) && choice.mounts_whole_console());
        term::platform::LinuxSubsystem next(dir, data_directory.c_str(), shared_directory.c_str(),
                                            console_directory.c_str());
        assert(next.mounts_whole_console());
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
        assert(next.start(80, 24) && live == 1);
        assert(access((console_directory + "/mock-file").c_str(), F_OK) == 0);
        assert(access((shared_directory + "/mock-file").c_str(), F_OK) != 0);
        assert(next.stop() && live == 0);
        unlink((console_directory + "/mock-file").c_str());
        destroyed = 0;
#endif
        assert(next.set_mounts_whole_console(false) && !next.mounts_whole_console());
        for (const char *damaged : {"", "/data", "//", "/x\n", "junk"})
        {
            int fd =
                open((data_directory + "/host-mount").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            assert(fd >= 0 && write(fd, damaged, std::strlen(damaged)) >= 0);
            close(fd);
            assert(!next.mounts_whole_console());
        }
        unlink((data_directory + "/host-mount").c_str());
        assert(!next.mounts_whole_console());
    }
#if defined(PS5TERM_LINUX_SHARED_FOLDER)
    {
        // An unavailable folder is a reported condition: Linux itself still starts.
        const std::string absent = base + "absent";
        term::platform::LinuxSubsystem vm(dir, data_directory.c_str(), absent.c_str(),
                                          console_directory.c_str());
        share_required = false;
        assert(vm.start(80, 24) && live == 1);
        char out[4];
        vm.read(out, sizeof out);
        assert(vm.status().find("could not be mounted") != std::string_view::npos);
        assert(vm.stop() && live == 0);
        share_required = true;
        destroyed = 0;
    }
#endif
    {
        term::platform::LinuxSubsystem vm(dir, data_directory.c_str(), shared_directory.c_str(),
                                          console_directory.c_str());
        assert(vm.start(80, 24) && live == 1);
        assert(!vm.start(80, 24) && live == 1);
        failed_worker = true;
        char out[4];
        vm.read(out, sizeof out);
        assert(!vm.running());
        failed_worker = false;
        assert(vm.start(80, 24) && live == 1 && destroyed == 1);
#if defined(PS5TERM_LINUX_NETWORK)
        assert(network_live == 1);
        network_state = -1;
        vm.read(out, sizeof out);
        assert(vm.status().find("network failed") != std::string_view::npos && vm.running());
#endif
        fail_stop = true;
        assert(!vm.stop() && live == 1);
#if defined(PS5TERM_LINUX_NETWORK)
        assert(network_live == 1); // Failed join must retain the borrowed backend.
#endif
        assert(!vm.start(80, 24) && live == 1);
#if defined(PS5TERM_LINUX_PERSISTENCE)
        // The running guest's disk cannot be deleted from under it.
        assert(vm.delete_storage() == EBUSY && vm.storage_mib() == 16);
#endif
        fail_stop = false;
        assert(vm.stop());
        assert(vm.stop());
        assert(live == 0);
        fail_start = true;
        assert(!vm.start(80, 24) && live == 0);
        fail_start = false;
#if defined(PS5TERM_LINUX_NETWORK)
        assert(network_live == 0);
        fail_network = true;
        assert(vm.start(80, 24));
        vm.read(out, sizeof out);
        assert(vm.status().find("offline") != std::string_view::npos);
        assert(vm.stop() && network_live == 0);
        fail_network = false;
        assert(network_dns == htonl(0x01010101)); // Default upstream resolver.
        network_state = 1;                        // Earlier assertion simulated a failed backend.
#if defined(PS5TERM_LINUX_PERSISTENCE)
        // The optional override is a strict dotted IPv4 file beside the data volume.
        const std::string resolver = data_directory + "/resolver";
        for (auto [text, expected] :
             {std::pair<const char *, uint32_t>{"192.168.86.1\n", 0xc0a85601},
              {"8.8.4.4", 0x08080404},
              {"127.0.0.1", 0x01010101},
              {"0.0.0.0\n", 0x01010101},
              {"224.0.0.1", 0x01010101},
              {"1.1.1.256", 0x01010101},
              {"10.0.0.1 extra", 0x01010101},
              {"1.2.3", 0x01010101}})
        {
            std::ofstream(resolver) << text;
            assert(vm.start(80, 24) && network_dns == htonl(expected));
            if (expected == 0xc0a85601)
            {
                vm.read(out, sizeof out);
                assert(vm.status().find("192.168.86.1") != std::string_view::npos);
            }
            assert(vm.stop());
        }
        unlink(resolver.c_str());
        assert(symlink("/dev/null", resolver.c_str()) == 0);
        assert(vm.start(80, 24) && network_dns == htonl(0x01010101));
        assert(vm.stop());
        unlink(resolver.c_str());
#endif
#endif
        kernel[56] = 0;
        put(base + "kernel-riscv64.bin", kernel);
        assert(!vm.start(80, 24) &&
               live == 0); // Stop released images; changed image is checked again.
        kernel[56] = 'R';
        put(base + "kernel-riscv64.bin", kernel);
        unlink((base + "bbl64.bin").c_str());
        assert(symlink("root-riscv64.bin", (base + "bbl64.bin").c_str()) == 0);
        assert(!vm.start(80, 24) && live == 0);
    }
#if defined(PS5TERM_LINUX_PERSISTENCE)
    {
        // Freeing the PS5's space (the PS5 keeps /data after an uninstall): the backup can go
        // at any time, the disk only while Linux is stopped; nothing else in the folder is
        // touched, and the next start seeds a new disk.
        term::platform::LinuxSubsystem vm(dir, data_directory.c_str(), shared_directory.c_str(),
                                          console_directory.c_str());
        const std::string disk = data_directory + "/data.ext2", backup = disk + ".backup",
                          other = data_directory + "/resolver";
        struct stat info
        {
        };
        assert(stat(disk.c_str(), &info) == 0 && vm.storage_mib() == 16);
        assert(vm.backup_mib() == 0 && vm.delete_backup() == 0); // nothing to delete is fine
        put(backup, std::vector<unsigned char>(3 * 1024 * 1024 + 1));
        put(disk + ".backup.new", std::vector<unsigned char>(10));
        std::ofstream(other) << "1.1.1.1";
        assert(vm.backup_mib() == 4); // rounded up: never shown as 0 while it exists
        assert(vm.delete_backup() == 0 && vm.backup_mib() == 0);
        assert(stat(backup.c_str(), &info) != 0 &&
               stat((disk + ".backup.new").c_str(), &info) != 0);
        assert(stat(disk.c_str(), &info) == 0 && stat(other.c_str(), &info) == 0);
        put(backup, std::vector<unsigned char>(100));
        assert(vm.delete_storage() == 0 && vm.storage_mib() == 0 && vm.backup_mib() == 0);
        assert(stat(disk.c_str(), &info) != 0 && stat(other.c_str(), &info) == 0);
        assert(vm.delete_storage() == 0); // again: already gone
        unlink(other.c_str());
    }
#endif
    for (auto name : {"bbl64.bin", "kernel-riscv64.bin", "root-riscv64.bin"})
        unlink((base + name).c_str());
#if defined(PS5TERM_LINUX_PERSISTENCE)
    unlink((base + "data-seed.ext2").c_str());
    unlink((data_directory + "/data.ext2").c_str());
    rmdir(data_directory.c_str());
#endif
    unlink((shared_directory + "/mock-file").c_str());
    rmdir(shared_directory.c_str());
    rmdir(console_directory.c_str());
    rmdir(data_directory.c_str());
    rmdir(dir);
    puts("Linux subsystem loader tests passed");
}
