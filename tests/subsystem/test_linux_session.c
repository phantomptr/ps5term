/* PS5Term - GPL-3.0-or-later. Host worker/session acceptance. */
#include "linux_session.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static double seconds(void)
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec / 1e9;
}
static void *load(const char *directory, const char *name, size_t *size)
{
    char path[2048];
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && length < 32 * 1024 * 1024);
    rewind(file);
    void *data = malloc((size_t)length);
    assert(data);
    assert(fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return data;
}
static void until(Ps5LinuxSession *session, const char *marker, char *output, size_t capacity)
{
    size_t size = 0;
    output[0] = 0;
    double deadline = seconds() + 20;
    while (!strstr(output, marker))
    {
        Ps5LinuxSessionStatus status = ps5_linux_session_status(session);
        assert(status.state != PS5_SESSION_FAILED);
        assert(seconds() < deadline);
        assert(size < capacity - 1);
        size += ps5_linux_session_read(session, output + size, capacity - size - 1);
        output[size] = 0;
        usleep(1000);
    }
}
static void *stop(void *opaque)
{
    assert(ps5_linux_session_stop(opaque));
    return NULL;
}
/* A real PS5 crash: the platform default stack could not hold a 65 KiB frame in
 * the network path, while every host default could. Measure it on the worker. */
static size_t worker_stack;
static int stack_write(void *context, const void *bytes, size_t size)
{
    (void)context;
    (void)bytes;
    (void)size;
    return 0;
}
static int stack_read(void *context, void *bytes, size_t size)
{
    (void)context;
    (void)bytes;
    (void)size;
    return 0;
}
static int stack_poll(void *context)
{
    (void)context;
    size_t size = 0;
#if defined(__APPLE__)
    size = pthread_get_stacksize_np(pthread_self());
#else
    pthread_attr_t attributes;
    assert(!pthread_getattr_np(pthread_self(), &attributes));
    assert(!pthread_attr_getstacksize(&attributes, &size));
    pthread_attr_destroy(&attributes);
#endif
    __atomic_store_n(&worker_stack, size, __ATOMIC_SEQ_CST);
    return 0;
}
static void check_worker_stack(Ps5LinuxSession *session, Ps5LinuxImages images)
{
    images.network = (Ps5LinuxNetwork){&worker_stack, stack_write, stack_poll, stack_read};
    assert(ps5_linux_session_start(session, &images));
    double deadline = seconds() + 10;
    size_t size;
    while (!(size = __atomic_load_n(&worker_stack, __ATOMIC_SEQ_CST)))
    {
        assert(seconds() < deadline);
        usleep(1000);
    }
    assert(size + 64 * 1024 >= PS5_LINUX_WORKER_STACK &&
           size <= PS5_LINUX_WORKER_STACK + 64 * 1024);
    assert(ps5_linux_session_stop(session));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    Ps5LinuxImages images = {0};
    images.bios = load(argv[1], "bbl64.bin", &images.bios_size);
    images.kernel = load(argv[1], "kernel-riscv64.bin", &images.kernel_size);
    images.disk = load(argv[1], "root-riscv64.bin", &images.disk_size);
    Ps5LinuxSession *session = ps5_linux_session_create(160 * 1024 * 1024);
    assert(session);
    assert(ps5_linux_session_stop(session));
    check_worker_stack(session, images);
    char *output = malloc(256 * 1024);
    assert(output);
    for (int iteration = 0; iteration < 3; iteration++)
    {
        assert(ps5_linux_session_start(session, &images));
        assert(!ps5_linux_session_start(session, &images));
        until(session, "~ # ", output, 256 * 1024);
        assert(ps5_linux_session_resize(session, 109, 33));
        const char *command = "test \"$(stty size)\" = '33 109' && printf 'WORKER_%s\\n' RESIZED\n";
        assert(ps5_linux_session_write(session, command, strlen(command)) == strlen(command));
        until(session, "WORKER_RESIZED\r\n", output, 256 * 1024);
        const char *flow =
            "dmesg -n 1; yes WORKER_FLOW | head -n 12000; printf 'WORKER_%s\\n' DONE\n";
        assert(ps5_linux_session_write(session, flow, strlen(flow)) == strlen(flow));
        double deadline = seconds() + 10;
        while (!ps5_linux_session_status(session).output_blocked)
        {
            assert(seconds() < deadline);
            assert(ps5_linux_session_status(session).state == PS5_SESSION_RUNNING);
            usleep(1000);
        }
        assert(ps5_linux_session_status(session).heap_used <= 160 * 1024 * 1024);
        until(session, "WORKER_DONE\r\n", output, 256 * 1024);
        const char *position = output;
        for (int i = 0; i < 12000; i++)
        {
            position = strstr(position, "WORKER_FLOW\r\n");
            assert(position);
            position += 13;
        }
        /* Two callers racing to stop must join exactly once, without deadlock. */
        pthread_t a, b;
        assert(!pthread_create(&a, NULL, stop, session));
        assert(!pthread_create(&b, NULL, stop, session));
        assert(!pthread_join(a, NULL));
        assert(!pthread_join(b, NULL));
        assert(ps5_linux_session_status(session).state == PS5_SESSION_STOPPED);
        assert(ps5_linux_session_status(session).heap_used == 0);
        assert(ps5_linux_session_read(session, output, 256 * 1024) == 0);
    }
    /* Cancel startup, then cancel a worker paused behind a non-reading client. */
    assert(ps5_linux_session_start(session, &images));
    assert(ps5_linux_session_stop(session));
    assert(ps5_linux_session_start(session, &images));
    until(session, "~ # ", output, 256 * 1024);
    /* A longer line fills the byte queues without tens of thousands of tiny
     * guest writes under the much slower race-detector interpreter. */
    const char *cancel = "yes CANCEL_BACKPRESSURE_TEST\n";
    assert(ps5_linux_session_write(session, cancel, strlen(cancel)) == strlen(cancel));
    double deadline = seconds() + 10;
    while (!ps5_linux_session_status(session).output_blocked)
    {
        assert(seconds() < deadline);
        usleep(1000);
    }
    assert(ps5_linux_session_stop(session));
    /* Natural guest shutdown must retain its final console bytes, even when
     * they were emitted in the same interpreter batch as the exit trap. */
    assert(ps5_linux_session_start(session, &images));
    until(session, "~ # ", output, 256 * 1024);
    const char *shutdown = "printf 'FINAL_%s\\n' OUTPUT; poweroff -f\n";
    assert(ps5_linux_session_write(session, shutdown, strlen(shutdown)) == strlen(shutdown));
    deadline = seconds() + 20;
    while (ps5_linux_session_status(session).state == PS5_SESSION_RUNNING ||
           ps5_linux_session_status(session).state == PS5_SESSION_DRAINING)
    {
        assert(seconds() < deadline);
        usleep(1000);
    }
    assert(ps5_linux_session_status(session).state == PS5_SESSION_STOPPED);
    size_t final = ps5_linux_session_read(session, output, 256 * 1024 - 1);
    output[final] = 0;
    if (!strstr(output, "Power down"))
    {
        fprintf(stderr, "Shutdown lost tail: %s\n", output);
        abort();
    }
    assert(strstr(output, "FINAL_OUTPUT\r\n"));
    assert(ps5_linux_session_stop(session));
    ps5_linux_session_destroy(session);
    Ps5LinuxSession *small = ps5_linux_session_create(1024);
    assert(small);
    assert(ps5_linux_session_start(small, &images));
    deadline = seconds() + 5;
    while (ps5_linux_session_status(small).state != PS5_SESSION_FAILED)
    {
        assert(seconds() < deadline);
        usleep(1000);
    }
    assert(ps5_linux_session_status(small).error == PS5_LINUX_MEMORY);
    assert(ps5_linux_session_stop(small));
    ps5_linux_session_destroy(small);
    free(output);
    free((void *)images.bios);
    free((void *)images.kernel);
    free((void *)images.disk);
    puts("PASS: worker lifecycle, concurrent stop/join, startup cancellation, resize, lossless "
         "slow reader, stop while blocked, natural shutdown tail and OOM cleanup");
    return 0;
}
