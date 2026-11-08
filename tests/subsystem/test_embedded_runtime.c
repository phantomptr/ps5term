/* PS5Term - GPL-3.0-or-later. Host-only real guest lifecycle acceptance. */
#include "embedded_linux.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned char *load(const char *directory, const char *name, size_t *size)
{
    char path[2048];
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && length < 32 * 1024 * 1024);
    rewind(file);
    unsigned char *data = malloc((size_t)length);
    assert(data);
    assert(fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return data;
}
static double seconds(void)
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec / 1e9;
}
static void collect(Ps5Linux *vm, char *text, size_t *used, size_t capacity)
{
    size_t count = ps5_linux_read(vm, text + *used, capacity - *used - 1);
    *used += count;
    text[*used] = 0;
    assert(*used < capacity - 1);
}
static void until(Ps5Linux *vm, const char *marker, char *text, size_t *used, size_t capacity)
{
    double deadline = seconds() + 20;
    while (!strstr(text, marker))
    {
        if (seconds() >= deadline)
        {
            fprintf(stderr, "Timeout waiting for %s; guest output:\n%s\n", marker, text);
            abort();
        }
        assert(ps5_linux_step(vm, 10000) == PS5_LINUX_RUNNING);
        collect(vm, text, used, capacity);
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    Ps5LinuxImages images = {0};
    images.bios = load(argv[1], "bbl64.bin", &images.bios_size);
    images.kernel = load(argv[1], "kernel-riscv64.bin", &images.kernel_size);
    images.disk = load(argv[1], "root-riscv64.bin", &images.disk_size);
    unsigned char *disk_before = malloc(images.disk_size);
    assert(disk_before);
    memcpy(disk_before, images.disk, images.disk_size);
    Ps5Linux *vm = ps5_linux_create(160 * 1024 * 1024);
    assert(vm);
    assert(ps5_linux_test_allocator(vm));
    assert(ps5_linux_step(vm, 1) == PS5_LINUX_STOPPED);
    Ps5LinuxImages invalid = images;
    invalid.disk_size = 513;
    assert(!ps5_linux_start(vm, &invalid));
    assert(ps5_linux_heap_used(vm) == 0);
    /* Each interrupted construction must discard its entire partial graph. */
    for (size_t cutoff = 0; cutoff < 40; cutoff++)
    {
        ps5_linux_test_fail_after(vm, cutoff);
        int started = ps5_linux_start(vm, &images);
        if (!started)
            assert(ps5_linux_state(vm) == PS5_LINUX_FAILED);
        ps5_linux_stop(vm);
        assert(ps5_linux_heap_used(vm) == 0);
    }
    ps5_linux_test_fail_after(vm, (size_t)-1);
    char *text = malloc(256 * 1024);
    assert(text);
    for (int iteration = 0; iteration < 5; iteration++)
    {
        size_t used = 0;
        text[0] = 0;
        assert(ps5_linux_start(vm, &images));
        assert(!ps5_linux_start(vm, &images)); /* no implicit replacement */
        assert(ps5_linux_step(vm, 0) == PS5_LINUX_RUNNING);
        assert(ps5_linux_step(vm, 50001) == PS5_LINUX_RUNNING);
        assert(!ps5_linux_resize(vm, 0, 30));
        assert(ps5_linux_resize(vm, 97, 31));
        until(vm, "~ # ", text, &used, 256 * 1024);
        assert(ps5_linux_resize(vm, 113, 37));
        const char *command =
            "printf '\\nINPROC_%s\\n' BEGIN; test \"$(stty size)\" = '37 113' && printf "
            "'INPROC_%s\\n' RESIZE; f() { echo shell; }; test \"$(f)\" = shell && printf "
            "'INPROC_%s\\n' FUNCTION; printf 'b\\na\\n' | sort > /tmp/probe; test \"$(head -n 1 "
            "/tmp/probe)\" = a && printf 'INPROC_%s\\n' PIPELINE; sleep 0 & c=$!; wait \"$c\" && "
            "printf 'INPROC_%s\\n' WAIT; mkdir -p /dev/pts; mount -t devpts devpts /dev/pts; "
            "script -q -c 'stty rows 31 cols 97; stty size' /tmp/pty; grep -q '31 97' /tmp/pty && "
            "printf 'INPROC_%s\\n' PTY; printf 'INPROC_%s\\n' DONE\n";
        assert(ps5_linux_write(vm, command, strlen(command)) == strlen(command));
        until(vm, "INPROC_DONE\r\n", text, &used, 256 * 1024);
        assert(strstr(text, "INPROC_FUNCTION\r\n"));
        assert(strstr(text, "INPROC_PIPELINE\r\n"));
        assert(strstr(text, "INPROC_WAIT\r\n"));
        assert(strstr(text, "INPROC_PTY\r\n"));
        assert(strstr(text, "INPROC_RESIZE\r\n"));
        assert(ps5_linux_heap_used(vm) <= 160 * 1024 * 1024);
        ps5_linux_stop(vm);
        ps5_linux_stop(vm);
        assert(ps5_linux_state(vm) == PS5_LINUX_STOPPED);
        assert(ps5_linux_heap_used(vm) == 0);
        assert(!memcmp(disk_before, images.disk, images.disk_size));
    }
    /* Input queue is bounded, with explicit partial acceptance. */
    assert(ps5_linux_start(vm, &images));
    char *large = malloc(128 * 1024);
    assert(large);
    memset(large, 'x', 128 * 1024);
    assert(ps5_linux_write(vm, large, 128 * 1024) == PS5_LINUX_INPUT_CAPACITY);
    assert(ps5_linux_write(vm, large, 1) == 0);
    free(large);
    ps5_linux_stop(vm);
    /* Runtime allocation failure must stop the guest and release its graph. */
    assert(ps5_linux_start(vm, &images));
    size_t used = 0;
    text[0] = 0;
    until(vm, "~ # ", text, &used, 256 * 1024);
    ps5_linux_test_fail_after(vm, 0);
    const char *echo = "echo fail-me\n";
    assert(ps5_linux_write(vm, echo, strlen(echo)) == strlen(echo));
    double deadline = seconds() + 5;
    while (ps5_linux_state(vm) == PS5_LINUX_RUNNING)
    {
        assert(seconds() < deadline);
        ps5_linux_step(vm, 10000);
    }
    assert(ps5_linux_error(vm) == PS5_LINUX_MEMORY);
    assert(ps5_linux_heap_used(vm) == 0);
    ps5_linux_test_fail_after(vm, SIZE_MAX);
    assert(ps5_linux_start(vm, &images));
    used = 0;
    text[0] = 0;
    until(vm, "~ # ", text, &used, 256 * 1024);
    /* Keep asynchronous kernel notices from interleaving exact line markers. */
    const char *flow = "dmesg -n 1; yes FLOW | head -n 20000; printf 'FLOW_%s\\n' DONE\n";
    assert(ps5_linux_write(vm, flow, strlen(flow)) == strlen(flow));
    deadline = seconds() + 2;
    while (seconds() < deadline && ps5_linux_state(vm) == PS5_LINUX_RUNNING)
        ps5_linux_step(vm, 10000);
    assert(ps5_linux_state(vm) == PS5_LINUX_RUNNING); /* slow reader cannot kill guest */
    used = 0;
    text[0] = 0;
    deadline = seconds() + 20;
    while (!strstr(text, "FLOW_DONE\r\n"))
    {
        assert(seconds() < deadline);
        assert(ps5_linux_step(vm, 10000) == PS5_LINUX_RUNNING);
        size_t count = ps5_linux_read(vm, text + used, 1024);
        used += count;
        assert(used < 256 * 1024);
        text[used] = 0;
    }
    const char *position = text;
    for (int line = 0; line < 20000; line++)
    {
        const char *marker = "FLOW\r\n";
        const char *found = strstr(position, marker);
        if (!found)
        {
            fprintf(stderr, "Missing flow line %d; bytes=%zu; output follows:\n", line, used);
            fwrite(text, 1, used, stderr);
            abort();
        }
        position = found + strlen(marker);
    }
    ps5_linux_destroy(vm);
    Ps5Linux *small = ps5_linux_create(1024);
    assert(small);
    assert(!ps5_linux_start(small, &images));
    assert(ps5_linux_heap_used(small) == 0);
    ps5_linux_destroy(small);
    assert(!ps5_linux_create(PS5_LINUX_MAX_HEAP + 1));
    free(text);
    free(disk_before);
    free((void *)images.bios);
    free((void *)images.kernel);
    free((void *)images.disk);
    puts("PASS: in-process Linux, shell/PTYS/resize, 5 lifecycles, 40 construction cutoffs, "
         "runtime OOM, input bounds, lossless slow-reader flow and disk bounds");
    return 0;
}
