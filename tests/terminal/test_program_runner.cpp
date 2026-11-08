// PS5Term GPL-3.0-or-later. `run` end to end on the host: the PS5 shell asks, the tab's
// connection writes the request and sends the launcher to a fake ELF loader, and a fake
// launcher (using the payload's own request parser) connects back and plays a program.
#include "fake_run_loader.hpp"
#include <chrono>
#include <cstdlib>

namespace
{
struct Tab
{
    term::TerminalModel model{24, 80};
    term::Connection connection{no_pty()};
    std::uint64_t now = 1000000;
    void tick(int times = 1)
    {
        for (int i = 0; i < times; ++i)
        {
            connection.tick(model, now += 10000);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    bool wait_for(std::string_view text, int limit = 2000)
    {
        for (int i = 0; i < limit; ++i)
        {
            tick();
            if (screen_has(model, text))
                return true;
        }
        std::fprintf(stderr, "waiting for [%.*s]; the screen shows:\n",
                     static_cast<int>(text.size()), text.data());
        for (int r = 0; r < model.rows(); ++r)
        {
            std::string line;
            for (int c = 0; c < model.columns(); ++c)
            {
                const auto cp = model.cell(r, c).codepoints[0];
                line += cp >= 0x20 && cp <= 0x7e ? static_cast<char>(cp) : ' ';
            }
            if (line.find_first_not_of(' ') != std::string::npos)
                std::fprintf(stderr, "  |%s\n", line.c_str());
        }
        return false;
    }
};
} // namespace

int main()
{
    char directory[] = "/tmp/ps5term-run-XXXXXX";
    assert(mkdtemp(directory));
    const std::string launcher = std::string(directory) + "/ps5term-run.elf",
                      request = std::string(directory) + "/run/request",
                      terminfo = std::string(directory) + "/terminfo";
    FILE *file = std::fopen(launcher.c_str(), "wb");
    std::fwrite(launcher_bytes.data(), 1, launcher_bytes.size(), file);
    std::fclose(file);

    for (const bool interrupt : {true, false})
    {
        FakeLoader loader;
        loader.request_path = request;
        loader.start(interrupt);
        Tab tab;
        term::platform::ProgramRunner::Config config;
        config.request_path = request.c_str();
        config.launcher_path = launcher.c_str();
        config.terminfo_source = launcher.c_str(); // any file stands in for the entry
        config.terminfo_directory = terminfo.c_str();
        config.loader_port = loader.port;
        config.connect_timeout_ms = 5000;
        tab.connection.configure_programs(config);
        tab.tick(5);
        assert(tab.connection.submit("cd /tmp"));
        tab.tick(5);
        assert(tab.connection.submit(("run " + launcher + " \"two words\" x").c_str()));
        assert(tab.wait_for("name?"));
        assert(tab.connection.running_program() && !tab.connection.native());
        assert(loader.received_arguments == launcher + "|two words|x|");
        assert(access((terminfo + "/x/xterm-256color").c_str(), R_OK) == 0);
        // Another user (the payload) must read both; seen on the console with 0600.
        struct stat info
        {
        };
        assert(stat((terminfo + "/x/xterm-256color").c_str(), &info) == 0 &&
               (info.st_mode & 0777) == 0644);
        assert(stat((std::string(directory) + "/run").c_str(), &info) == 0 &&
               (info.st_mode & 0711) == 0711);
        // Cooked: the app's line editor sends a whole line.
        assert(tab.connection.submit("Ada"));
        assert(tab.wait_for("FULL SCREEN"));
        assert(screen_has(tab.model, "hi Ada") || tab.model.alternate_screen());
        tab.tick(3);
        assert(tab.connection.native());             // Full screen: keys go straight through.
        assert(tab.connection.write_raw("a\rb", 3)); // Enter arrives as LF
        if (interrupt)
        {
            assert(tab.wait_for("keys=a"));
            assert(tab.connection.write_raw("\x03", 1)); // Ctrl+C: SIGINT, not a key
            assert(tab.wait_for("[ps5term-run.elf stopped by signal 2]"));
            assert(loader.got_interrupt);
        }
        else
            assert(tab.wait_for("[ps5term-run.elf exited with status 5]"));
        // The PS5 shell is back, on the normal screen, with its prompt.
        assert(!tab.connection.running_program() && !tab.model.alternate_screen());
        assert(tab.connection.submit("echo back"));
        assert(tab.wait_for("back"));
        loader.thread.join();
    }

    // No loader on the port: a clear message, and the shell carries on.
    {
        unsigned closed_port = 0;
        close(listen_any(closed_port));
        Tab tab;
        term::platform::ProgramRunner::Config config;
        config.request_path = request.c_str();
        config.launcher_path = launcher.c_str();
        config.terminfo_directory = terminfo.c_str();
        config.loader_port = closed_port;
        config.connect_timeout_ms = 500;
        tab.connection.configure_programs(config);
        tab.tick(5);
        assert(tab.connection.submit(("run " + launcher).c_str()));
        assert(tab.wait_for("no ELF loader on port 9021"));
        assert(!tab.connection.running_program());
        assert(tab.connection.submit("echo still here"));
        assert(tab.wait_for("still here"));
    }
    // Missing program: the shell says so without trying.
    {
        Tab tab;
        tab.tick(5);
        assert(tab.connection.submit("run /tmp/does-not-exist-ps5term"));
        assert(tab.wait_for("No such file"));
        assert(!tab.connection.running_program());
    }
    // A shell without a tab (SSH) refuses.
    {
        term::BuiltinShell shell("/tmp", false);
        assert(shell.submit(("run " + launcher).c_str()));
        char out[256]{};
        for (int i = 0; i < 50 && !shell.read_error(out, sizeof out - 1); ++i)
            shell.tick();
        assert(std::strstr(out, "not over SSH"));
    }
    unlink(launcher.c_str());
    unlink((terminfo + "/x/xterm-256color").c_str());
    rmdir((terminfo + "/x").c_str());
    rmdir(terminfo.c_str());
    rmdir((std::string(directory) + "/run").c_str());
    rmdir(directory);
    std::puts("PASS: run: request, loader, launcher token, line and raw input, Ctrl+C as SIGINT, "
              "exit status, missing loader and program, SSH refusal");
}
