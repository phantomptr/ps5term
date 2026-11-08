// PS5Term - GPL-3.0-or-later.
#include "ssh/command_session.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <unistd.h>
using term::ssh::CommandSession;
using term::ssh::SessionMode;
struct Output
{
    std::string out, err;
};
Output drain(CommandSession &session, unsigned ticks = 1000)
{
    Output output;
    char bytes[47];
    for (unsigned i = 0; i < ticks; ++i)
    {
        session.tick();
        while (auto n = session.read_stdout(bytes, sizeof bytes))
            output.out.append(bytes, n);
        while (auto n = session.read_stderr(bytes, sizeof bytes))
            output.err.append(bytes, n);
        if (session.finished())
            break;
    }
    return output;
}
int main()
{
    CommandSession exec(SessionMode::exec, "echo clean", "/tmp");
    auto result = drain(exec);
    assert(result.out == "clean\n" && result.err.empty());
    assert(exec.finished() && exec.exit_status() == 0 && exec.write("extra") == 0);
    CommandSession bad(SessionMode::exec, "not-a-command", "/tmp");
    result = drain(bad);
    assert(result.out.empty() && !result.err.empty());
    assert(bad.finished() && bad.exit_status() == 127);
    CommandSession invalid(SessionMode::exec, std::string(513, 'x'), "/tmp");
    result = drain(invalid);
    assert(invalid.finished() && invalid.exit_status() == 2 && !result.err.empty());
    CommandSession a(SessionMode::shell, "", "/tmp"), b(SessionMode::shell, "", "/");
    assert(drain(a).out == "/tmp$ ");
    assert(drain(b).out == "/$ ");
    assert(a.write("echo hel") == 8);
    assert(a.write("lo\r") == 3);
    assert(a.write("\necho two\n") == 10);
    result = drain(a);
    assert(result.out == "echo hello\r\nhello\n/tmp$ echo two\r\ntwo\n/tmp$ ");
    assert(a.write("cd /\npwd\n") == 9);
    result = drain(a);
    assert(result.out.find("/$ pwd\r\n/\n/$ ") != std::string::npos);
    assert(b.write("pwd") == 3);
    b.finish_input();
    result = drain(b);
    assert(b.finished() && result.out == "pwd\r\n/\n/$ ");
    // A rejected overlong line never runs a truncated prefix.
    std::string overlong = "echo " + std::string(600, 'x') + "\necho safe\n";
    assert(a.write(overlong) == overlong.size());
    result = drain(a);
    assert(!result.err.empty() && result.out.find("\nsafe\n") != std::string::npos);
    assert(result.out.find(std::string(512, 'x') + "\n") == std::string::npos);
    assert(a.write("echo oops\x7fok\n") == 13);
    result = drain(a);
    assert(result.out.find("\noopok\n") != std::string::npos);
    // Escape sequences from cursor keys are swallowed, never command text.
    assert(a.write("echo x\x1b[A\n") == 10);
    result = drain(a);
    assert(result.out.find("\nx\n") != std::string::npos);
    assert(a.write("echo never") == 10);
    assert(a.write(std::string_view("\x03", 1)) == 1);
    result = drain(a);
    assert(result.out.find("^C") != std::string::npos);
    assert(a.write("exit\necho never\n") == 16);
    result = drain(a);
    assert(a.finished() && result.out.find("never") == std::string::npos);
    // Backpressure consumes a prefix, leaving the transport responsible for its tail.
    CommandSession slow(SessionMode::shell, "", "/tmp");
    drain(slow);
    std::string many;
    for (unsigned i = 0; i < 3000; ++i)
        many += "echo z\n";
    auto accepted = slow.write(many);
    assert(accepted > 0 && accepted < many.size());
    std::size_t position = accepted;
    std::string all;
    for (unsigned i = 0; i < 50000 && !slow.finished(); ++i)
    {
        slow.tick();
        char bytes[31];
        auto n = slow.read_stdout(bytes, sizeof bytes);
        all.append(bytes, n);
        char errors[64];
        assert(slow.read_stderr(errors, sizeof errors) == 0);
        if (position < many.size())
            position += slow.write(std::string_view(many).substr(position));
        if (position == many.size())
            slow.finish_input();
    }
    assert(slow.finished());
    unsigned count = 0;
    for (std::size_t p = 0; (p = all.find("\nz\n", p)) != std::string::npos; p += 3)
        ++count;
    assert(count == 3000);
    CommandSession rejected(SessionMode::shell, "", "/tmp");
    drain(rejected);
    std::string nul = "echo unsafe";
    nul.push_back(0);
    nul += "tail\n";
    assert(rejected.write(nul) == nul.size());
    rejected.finish_input();
    result = drain(rejected);
    assert(rejected.finished() && rejected.exit_status() == 2 && !result.err.empty());
    assert(result.out.find("\nunsafetail\n") == std::string::npos);
    CommandSession interrupted(SessionMode::shell, "", "/tmp");
    drain(interrupted);
    assert(interrupted.write(std::string_view("\x03", 1)) == 1);
    interrupted.finish_input();
    drain(interrupted);
    assert(interrupted.finished() && interrupted.exit_status() == 130);
    // Disconnect closes the interpreter's active file and discards owned queues.
    char pattern[] = "/tmp/ps5term-ssh-XXXXXX";
    auto root = mkdtemp(pattern);
    assert(root);
    {
        std::ofstream file(std::string(root) + "/large");
        file << std::string(20000, 'q');
    }
    CommandSession complete(SessionMode::exec, "cat large", root);
    for (unsigned i = 0; i < 100; ++i)
        complete.tick(); // deliberately don't read
    result = drain(complete);
    assert(complete.finished() && complete.exit_status() == 0);
    assert(result.out == std::string(20000, 'q') &&
           result.err.empty()); // Exact bytes: exec adds nothing.
    const auto descriptor_count = []
    {
        unsigned count = 0;
        for (int fd = 0; fd < 256; ++fd)
            if (fcntl(fd, F_GETFD) != -1)
                ++count;
        return count;
    };
    const auto before = descriptor_count();
    CommandSession streaming(SessionMode::exec, "cat large", root);
    streaming.tick();
    assert(descriptor_count() == before + 1);
    streaming.close();
    assert(descriptor_count() == before);
    assert(streaming.finished() && streaming.write("x") == 0);
    assert(drain(streaming).out.empty());
    std::filesystem::remove_all(root);
    std::puts("SSH command session tests passed");
}
