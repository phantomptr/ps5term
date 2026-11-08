// PS5Term GPL-3.0-or-later. `run` through the whole app: typed keys reach the PS5 shell's
// line editor, then the program; once it shows a full screen, keys go to it directly.
#include "app.hpp"
#include "fake_run_loader.hpp"
#include <chrono>
#include <new>

namespace term
{
// No console devices here: the app's input, mouse and research ticks do nothing.
void Input::tick(std::uint64_t, KeyHandler) noexcept
{
}
void Research::tick(TerminalModel &, std::uint64_t) noexcept
{
}
void Research::keyboard(std::uint32_t, const std::uint16_t *, int, bool, bool) noexcept
{
}
int Mouse::poll(std::uint64_t, MouseSample *, int) noexcept
{
    return 0;
}
struct AppTest
{
    static void type(App &app, std::string_view text)
    {
        for (char ch : text)
            app.key(Key::character, ch);
        app.key(Key::enter, 0);
    }
    static bool wait_for(App &app, std::uint64_t &now, bool (*done)(App &), int limit = 3000)
    {
        for (int i = 0; i < limit; ++i)
        {
            app.tick(now += 10000);
            if (done(app))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::fprintf(stderr, "timed out; native=%d alternate=%d; the screen shows:\n",
                     app.active_connection().native(), app.model_.alternate_screen());
        print_screen(app.model_);
        return false;
    }
    static void run(const std::string &launcher, const std::string &request,
                    const std::string &terminfo)
    {
        FakeLoader loader;
        loader.request_path = request;
        loader.start(true);
        App app;
        // As on the console: no PTY, so the first tab is the built-in PS5 shell.
        app.connection_.~Connection();
        new (&app.connection_) Connection(no_pty());
        platform::ProgramRunner::Config config;
        config.request_path = request.c_str();
        config.launcher_path = launcher.c_str();
        config.terminfo_source = launcher.c_str();
        config.terminfo_directory = terminfo.c_str();
        config.loader_port = loader.port;
        app.connection_.configure_programs(config);
        std::uint64_t now = 10000000;
        for (int i = 0; i < 5; ++i)
            app.tick(now += 10000);
        type(app, "cd /tmp");
        for (int i = 0; i < 5; ++i)
            app.tick(now += 10000);
        type(app, "run " + launcher + " x");
        assert(wait_for(app, now, [](App &a) { return screen_has(a.model_, "name?"); }));
        assert(!app.active_connection().native()); // Cooked: the line editor collects a line.
        type(app, "Ada");
        assert(wait_for(app, now, [](App &a) { return a.model_.alternate_screen(); }));
        app.tick(now += 10000);
        assert(app.active_connection().native()); // Full screen: keys go straight to it.
        for (char ch : std::string_view("abc"))
            app.key(Key::character, ch);
        assert(app.editor_.text().empty());
        assert(wait_for(app, now, [](App &a) { return screen_has(a.model_, "keys=abc"); }));
        app.key(Key::interrupt, 0); // Ctrl+C: SIGINT to the program.
        assert(
            wait_for(app, now, [](App &a) { return screen_has(a.model_, "stopped by signal 2"); }));
        assert(!app.active_connection().native() && !app.model_.alternate_screen());
        loader.thread.join();
    }
};
} // namespace term

int main()
{
    char directory[] = "/tmp/ps5term-app-run-XXXXXX";
    assert(mkdtemp(directory));
    const std::string launcher = std::string(directory) + "/ps5term-run.elf",
                      request = std::string(directory) + "/run/request",
                      terminfo = std::string(directory) + "/terminfo";
    FILE *file = std::fopen(launcher.c_str(), "wb");
    std::fwrite(launcher_bytes.data(), 1, launcher_bytes.size(), file);
    std::fclose(file);
    term::AppTest::run(launcher, request, terminfo);
    std::system((std::string("rm -rf ") + directory).c_str());
    std::puts("PASS: run through the app: line editor, full-screen keys, Ctrl+C");
}
