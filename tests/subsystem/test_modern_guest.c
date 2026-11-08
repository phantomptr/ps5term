/* PS5Term - GPL-3.0-or-later. Real modern guest acceptance, host only. */
#include "embedded_linux.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void)
{
    struct timespec value;
    assert(!clock_gettime(CLOCK_MONOTONIC, &value));
    return value.tv_sec + value.tv_nsec / 1e9;
}
static void *load(const char *directory, const char *name, size_t *size)
{
    char path[2048];
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && length <= 32 * 1024 * 1024);
    rewind(file);
    void *data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return data;
}
static void send(Ps5Linux *vm, const char *command)
{
    assert(ps5_linux_write(vm, command, strlen(command)) == strlen(command));
}
static void until(Ps5Linux *vm, const char *marker, char *output, size_t capacity)
{
    size_t used = strlen(output);
    double deadline = now() + 90;
    while (!strstr(output, marker))
    {
        Ps5LinuxState state = ps5_linux_step(vm, 50000);
        size_t count = ps5_linux_read(vm, output + used, capacity - used - 1);
        if (count)
            fwrite(output + used, 1, count, stdout);
        used += count;
        output[used] = 0;
        if (state != PS5_LINUX_RUNNING || now() >= deadline || used == capacity - 1 ||
            strstr(output, "sbi_trap_error:") || strstr(output, "Kernel panic"))
        {
            fprintf(stderr, "\nGuest failed waiting for %s (state=%d error=%d heap=%zu)\n", marker,
                    state, ps5_linux_error(vm), ps5_linux_heap_used(vm));
            abort();
        }
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    setvbuf(stdout, NULL, _IONBF, 0);
    Ps5LinuxImages images = {0};
    images.bios = load(argv[1], "bbl64.bin", &images.bios_size);
    images.kernel = load(argv[1], "kernel-riscv64.bin", &images.kernel_size);
    images.disk = load(argv[1], "root-riscv64.bin", &images.disk_size);
    void *before = malloc(images.disk_size);
    assert(before);
    memcpy(before, images.disk, images.disk_size);
    char *output = malloc(256 * 1024);
    assert(output);
    Ps5Linux *vm = ps5_linux_create(160 * 1024 * 1024);
    assert(vm);
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        double start = now();
        assert(ps5_linux_start(vm, &images));
        assert(ps5_linux_resize(vm, 97, 31));
        output[0] = 0;
        until(vm, " # ", output, 256 * 1024);
        printf("\nGuest booted in %.3fs\n", now() - start);
        assert(ps5_linux_resize(vm, 113, 37));
        output[0] = 0;
        send(vm,
             "dmesg -n 1; case $(uname -r) in 6.12.*-ps5term) printf 'MODERN_%s\\n' KERNEL;; esac; "
             "f() { printf '%s\\n' posix; }; test \"$(f | tr a-z A-Z)\" = POSIX && printf "
             "'MODERN_%s\\n' SHELL; "
             "(exit 7) & p=$!; wait $p; test $? = 7 && printf 'MODERN_%s\\n' WAIT; "
             "test \"$(stty size)\" = '37 113' && printf 'MODERN_%s\\n' RESIZE; "
             "script -q -c 'stty rows 23 cols 79; stty size' /tmp/pty; "
             "grep -q '23 79' /tmp/pty && printf 'MODERN_%s\\n' PTY; "
             "test \"$(tput colors)\" = 256 && printf 'MODERN_%s\\n' TERMINFO; "
             /* Hung the guest until firmware memory was reserved in the device tree. */
             "dd if=/dev/zero of=/tmp/fill bs=1048576 count=32 2>/dev/null && rm /tmp/fill && "
             "printf 'MODERN_%s\\n' MEMORY; "
             "printf 'MODERN_%s\\n' DONE\n");
        until(vm, "MODERN_DONE\r\n", output, 256 * 1024);
        const char *checks[] = {"KERNEL", "SHELL", "WAIT", "RESIZE", "PTY", "TERMINFO", "MEMORY"};
        for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i)
        {
            char marker[64];
            snprintf(marker, sizeof(marker), "MODERN_%s\r\n", checks[i]);
            if (!strstr(output, marker))
            {
                fprintf(stderr, "Missing %s\n", marker);
                abort();
            }
        }
        assert(ps5_linux_heap_used(vm) <= 160 * 1024 * 1024);
        ps5_linux_stop(vm);
        assert(ps5_linux_heap_used(vm) == 0);
        assert(!memcmp(before, images.disk, images.disk_size));
    }
    ps5_linux_destroy(vm);
    free(output);
    free(before);
    free((void *)images.bios);
    free((void *)images.kernel);
    free((void *)images.disk);
    puts("PASS: modern Linux kernel, POSIX shell, fork/wait, PTY, resize, terminfo, two boots and "
         "immutable base disk");
}
