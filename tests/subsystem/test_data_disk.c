/* PS5Term GPL-3.0-or-later. Optional durable disk reaches actual Linux. */
#include "embedded_linux.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef PS5TERM_TEST_FILE_DISK
extern Ps5LinuxDisk test_open_disk(const void *, size_t);
extern Ps5LinuxDisk test_reopen_disk(void);
extern void test_close_disk(void);
#else
static unsigned char data[16 * 1024 * 1024];
static unsigned reads, writes;
static int rd(void *opaque, uint64_t sector, void *bytes, size_t count)
{
    assert(opaque == data && sector <= sizeof(data) / 512 && count <= sizeof(data) / 512 - sector);
    memcpy(bytes, data + (size_t)sector * 512, count * 512);
    reads++;
    return 0;
}
static int wr(void *opaque, uint64_t sector, const void *bytes, size_t count)
{
    assert(opaque == data && sector <= sizeof(data) / 512 && count <= sizeof(data) / 512 - sector);
    memcpy(data + (size_t)sector * 512, bytes, count * 512);
    writes++;
    return 0;
}
#endif
static void *load(const char *root, const char *name, size_t *size)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    assert(n > 0);
    rewind(f);
    void *p = malloc((size_t)n);
    assert(p && fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (size_t)n;
    return p;
}
static void until(Ps5Linux *vm, const char *marker)
{
    char out[262144] = {0};
    size_t used = 0;
    time_t deadline = time(NULL) + 60;
    while (!strstr(out, marker))
    {
        assert(ps5_linux_step(vm, 50000) == PS5_LINUX_RUNNING);
        size_t n = ps5_linux_read(vm, out + used, sizeof(out) - used - 1);
        fwrite(out + used, 1, n, stdout);
        used += n;
        out[used] = 0;
        assert(time(NULL) < deadline && used < sizeof(out) - 1);
    }
}
static void command(Ps5Linux *vm, const char *s)
{
    assert(ps5_linux_write(vm, s, strlen(s)) == strlen(s));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    setvbuf(stdout, NULL, _IONBF, 0);
    Ps5LinuxImages images = {0};
    images.bios = load(argv[1], "bbl64.bin", &images.bios_size);
    images.kernel = load(argv[1], "kernel-riscv64.bin", &images.kernel_size);
    images.disk = load(argv[1], "root-riscv64.bin", &images.disk_size);
#ifdef PS5TERM_TEST_FILE_DISK
    size_t seed_size = 0;
    void *seed = load(argv[1], "data-seed.ext2", &seed_size);
    images.data_disk = test_open_disk(seed, seed_size);
    free(seed);
#else
    size_t seed_size = 0;
    void *seed = load(argv[1], "data-seed.ext2", &seed_size);
    assert(seed_size == sizeof(data));
    memcpy(data, seed, sizeof(data));
    free(seed);
    images.data_disk = (Ps5LinuxDisk){data, sizeof(data) / 512, rd, wr};
#endif
    Ps5Linux *vm = ps5_linux_create(160 * 1024 * 1024);
    assert(vm);
    assert(ps5_linux_start(vm, &images));
    until(vm, " # ");
    command(
        vm,
        "mkdir -p /mnt/data; grep -q ' /mnt/data ' /proc/mounts || mount /dev/vdb /mnt/data; echo "
        "retained-value >/mnt/data/ps5term-persist-test && sync && printf 'DATA_%s\\n' WRITTEN\n");
    until(vm, "DATA_WRITTEN\r\n");
#ifndef PS5TERM_TEST_FILE_DISK
    assert(reads && writes);
#endif
    ps5_linux_stop(vm);
#ifdef PS5TERM_TEST_FILE_DISK
    images.data_disk = test_reopen_disk();
#endif
    assert(ps5_linux_start(vm, &images));
    until(vm, " # ");
    command(vm, "mkdir -p /mnt/data; grep -q ' /mnt/data ' /proc/mounts || mount /dev/vdb "
                "/mnt/data; test \"$(cat /mnt/data/ps5term-persist-test)\" = retained-value && "
                "printf 'DATA_%s\\n' SURVIVED\n");
    until(vm, "DATA_SURVIVED\r\n");
    ps5_linux_stop(vm);
#ifdef PS5TERM_TEST_FILE_DISK
    test_close_disk();
#endif
    /* Invalid optional devices rejected before engine construction. */
    images.data_disk.read = NULL;
    assert(!ps5_linux_start(vm, &images));
    assert(ps5_linux_error(vm) == PS5_LINUX_ARGUMENT);
    ps5_linux_destroy(vm);
    free((void *)images.bios);
    free((void *)images.kernel);
    free((void *)images.disk);
    puts("PASS: optional data disk, guest mount/write/sync, stop/start persistence and invalid "
         "backend rejection");
}
