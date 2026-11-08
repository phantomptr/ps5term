// PS5Term - opt-in native capability service. GPL-3.0-or-later.
#include "platform/native_probe.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
using namespace term::platform;
namespace
{
int spawn(void *, int fd, void *, int &error) noexcept
{
    char marker = 'r';
    assert(write(fd, &marker, 1) == 1);
    error = 0;
    return 123;
}
int wait(void *, int pid, int &status, int &error) noexcept
{
    status = 23 << 8;
    error = 0;
    return pid;
}
int stop(void *, int) noexcept
{
    assert(false);
    return 0;
}
} // namespace
int main()
{
    char path[] = "/tmp/ps5term-probe-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    unlink(path);
    char request[256], report[256];
    std::snprintf(request, sizeof(request), "%s.request", path);
    std::snprintf(report, sizeof(report), "%s.report", path);
    NativeProbeService service;
    // No request means no report or resource allocation.
    service.tick(1, request, report);
    assert(access(report, F_OK) < 0);
    fd = open(request, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(write(fd, "wrong\n", 6) == 6);
    close(fd);
    service.tick(1000002, request, report);
    assert(access(report, F_OK) < 0);
    fd = open(request, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    constexpr char command[] = "baseline-v1\n";
    assert(write(fd, command, sizeof(command) - 1) == sizeof(command) - 1);
    close(fd);
    for (unsigned i = 0; i < 12; ++i)
        service.tick(2000003 + i, request, report);
    fd = open(report, O_RDONLY);
    assert(fd >= 0);
    char text[4096]{};
    auto n = read(fd, text, sizeof(text) - 1);
    close(fd);
    assert(n > 0 && n < static_cast<ssize_t>(sizeof(text) - 1));
    assert(std::strstr(text, "\"context\":\"app\""));
    assert(std::strstr(text, "\"operation\":\"pipe-poll\",\"state\":\"supported\""));
    assert(std::strstr(text, "\"operation\":\"spawn-wait\",\"state\":\"unknown\""));
    assert(std::strstr(text, "\"cleanup_ok\":true"));
    unlink(report);
    // Even a service with working child callbacks must wait for an explicit
    // child request; startup and the baseline above never invoke them.
    NativeProbeService child_service({nullptr, spawn, wait, stop});
    child_service.tick(1, request, report);
    assert(access(report, F_OK) < 0);
    fd = open(request, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(write(fd, "child-v1\n", 9) == 9);
    close(fd);
    for (unsigned i = 0; i < 15; ++i)
        child_service.tick(1000002 + i, request, report);
    fd = open(report, O_RDONLY);
    assert(fd >= 0);
    n = read(fd, text, sizeof(text) - 1);
    close(fd);
    assert(n > 0);
    text[n] = 0;
    assert(std::strstr(text, "\"scope\":\"child-experiment\""));
    assert(std::strstr(text, "\"complete\":true"));
    assert(std::strstr(text, "\"operation\":\"spawn-wait\",\"state\":\"supported\""));
    assert(std::strstr(text, "\"cleanup_ok\":true"));
    unlink(report);
}
