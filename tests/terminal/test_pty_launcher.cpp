// PS5Term - GPL-3.0-or-later.
#include "platform/pty_launcher.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
using namespace term::platform;

void wait_readable(PlatformLauncher &launcher, ChildId id, int ms)
{
    for (int i = 0; i < ms; i += 10)
    {
        char buf[1];
        auto r = launcher.read(id, buf, 1);
        if (r.state == IoState::ok || r.state == IoState::eof)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main()
{
    auto api = pty_launcher_create();
    assert(api.context);
    PlatformLauncher launcher(api);

    // Launch /bin/sh -c "echo hello"
    LaunchSpec spec{};
    spec.program = "/bin/sh";
    spec.generation = 1;
    auto result = launcher.start(spec);
    assert(result.child.valid());

    auto id = result.child;

    // Give the shell time to start.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send a command.
    const char *cmd = "echo hello-pty-test\n";
    auto wr = launcher.write(id, cmd, std::strlen(cmd));
    assert(wr.state == IoState::ok);

    // Read output — should contain "hello-pty-test".
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    char buf[4096]{};
    std::size_t total = 0;
    for (int attempt = 0; attempt < 50 && total < sizeof(buf) - 1; ++attempt)
    {
        auto rd = launcher.read(id, buf + total, sizeof(buf) - 1 - total);
        if (rd.state == IoState::ok)
            total += rd.count;
        else if (rd.state == IoState::eof)
            break;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    buf[total] = '\0';
    assert(std::strstr(buf, "hello-pty-test") != nullptr);

    // Test resize.
    assert(launcher.resize(id, 120, 40));

    // Test signal (interrupt).
    assert(launcher.signal(id, ChildSignal::interrupt));

    // Send exit.
    const char *exit_cmd = "exit 42\n";
    launcher.write(id, exit_cmd, std::strlen(exit_cmd));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Poll for exit.
    for (int i = 0; i < 100; ++i)
    {
        auto st = launcher.poll(id);
        if (st.phase == ChildPhase::exited)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Read remaining output until EOF.
    for (int i = 0; i < 20; ++i)
    {
        char drain[1024];
        auto rd = launcher.read(id, drain, sizeof drain);
        if (rd.state == IoState::eof)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Stop the child (should be already exited, but clean up).
    auto sr = launcher.stop(id);
    assert(sr.stopped);

    pty_launcher_destroy(api);
    std::puts("pty launcher tests passed");
}
