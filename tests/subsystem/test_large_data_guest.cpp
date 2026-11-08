// PS5Term GPL-3.0-or-later. Real guest expansion, Alpine chroot and restart.
#include "embedded_linux.h"
#include "net/nat.h"
#include "platform/linux_data_disk.hpp"
#include "platform/linux_shared_folder.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
static std::vector<uint8_t> load(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    assert(f);
    return {std::istreambuf_iterator<char>(f), {}};
}
static void until(Ps5Linux *vm, const char *marker, int seconds = 90)
{
    std::string out;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (out.find(marker) == out.npos)
    {
        assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING);
        char bytes[4096];
        const auto n = ps5_linux_read(vm, bytes, sizeof(bytes));
        fwrite(bytes, 1, n, stdout);
        out.append(bytes, n);
        assert(std::chrono::steady_clock::now() < end && out.size() < 1024 * 1024);
    }
}
static void command(Ps5Linux *vm, const char *s)
{
    assert(ps5_linux_write(vm, s, strlen(s)) == strlen(s));
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    setvbuf(stdout, nullptr, _IONBF, 0);
    auto bios = load(std::string(argv[1]) + "/bbl64.bin"),
         kernel = load(std::string(argv[1]) + "/kernel-riscv64.bin"),
         root = load(std::string(argv[1]) + "/root-riscv64.bin");
    char path[] = "/tmp/ps5term-large-XXXXXX";
    assert(mkdtemp(path));
    const std::string dir = path;
    std::filesystem::copy_file(std::string(argv[1]) + "/data-seed.ext2", dir + "/data.ext2");
    term::platform::LinuxDiskExpansion grow;
    assert(grow.start(path, 512));
    while (grow.busy())
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!grow.error());
    term::platform::LinuxDataDisk data;
    term::platform::LinuxSharedFolder share;
    assert(share.open(argv[2]));
    Ps5LinuxNatConfig config{};
    config.dns_ipv4 = inet_addr("1.1.1.1");
    config.dns_port = 53;
    config.fail_after = SIZE_MAX;
    auto *nat = ps5_linux_nat_create(&config);
    assert(nat);
    Ps5LinuxImages images{};
    images.bios = bios.data();
    images.bios_size = bios.size();
    images.kernel = kernel.data();
    images.kernel_size = kernel.size();
    images.disk = root.data();
    images.disk_size = root.size();
    images.wall_clock = time(nullptr);
    unsigned char entropy[64];
    int random = open("/dev/urandom", O_RDONLY);
    assert(random >= 0 && read(random, entropy, sizeof(entropy)) == sizeof(entropy));
    close(random);
    images.rng_seed = entropy;
    images.rng_seed_size = sizeof(entropy);
    images.shared_folder = share.device();
    images.network = ps5_linux_nat_device(nat);
    auto *vm = ps5_linux_create(160 * 1024 * 1024);
    assert(vm);
    int parent = open(path, O_RDONLY | O_DIRECTORY);
    assert(parent >= 0);
    for (int round = 0; round < 2; round++)
    {
        assert(data.open(parent, -1, 16 * 1024 * 1024, path));
        images.data_disk = data.device();
        assert(images.data_disk.sectors == 512 * 1024 * 2);
        assert(ps5_linux_start(vm, &images));
        until(vm, " # ");
        if (!round)
        {
            command(
                vm,
                "df -Pk /mnt/data; test $(df -Pk /mnt/data | awk 'END {print $2}') -gt 400000 && "
                "dd if=/dev/zero of=/mnt/data/large bs=1M count=24 && sync && printf 'LARGE_%s\\n' "
                "WRITTEN\n");
            until(vm, "LARGE_WRITTEN\r\n");
            command(vm,
                    "ps5term-alpine install /mnt/ps5/alpine-minirootfs-3.23.8-riscv64.tar.gz && "
                    "ps5term-alpine run apk --version && ps5term-alpine run sh -c 'echo kept "
                    ">/root/proof' && printf 'ALPINE_%s\\n' READY\n");
            until(vm, "ALPINE_READY\r\n", 180);
            if (std::string(argv[3]) == "online")
            {
                command(vm, "ps5term-alpine run apk add --no-cache bash && ps5term-alpine run "
                            "bash --version && printf 'APK_%s\\n' INSTALLED\n");
                until(vm, "APK_INSTALLED\r\n", 180);
            }
        }
        else
        {
            command(vm, "test $(wc -c </mnt/data/large) -eq 25165824 && "
                        "ps5term-alpine run sh -c 'test $(cat /root/proof) = kept' && printf "
                        "'LARGE_%s\\n' SURVIVED\n");
            until(vm, "LARGE_SURVIVED\r\n");
            if (std::string(argv[3]) == "online")
            {
                command(vm, "ps5term-alpine run bash --version && printf 'APK_%s\\n' SURVIVED\n");
                until(vm, "APK_SURVIVED\r\n");
            }
        }
        command(vm, "sync; mount -o remount,ro /mnt/data && printf 'DATA_%s\\n' CLEAN\n");
        until(vm, "DATA_CLEAN\r\n");
        ps5_linux_stop(vm);
        data.close();
    }
    ps5_linux_destroy(vm);
    ps5_linux_nat_destroy(nat);
    close(parent);
    std::filesystem::remove_all(dir);
    puts("PASS: 512 MiB real guest expansion, 24 MiB data, Alpine/apk and clean restart");
}
