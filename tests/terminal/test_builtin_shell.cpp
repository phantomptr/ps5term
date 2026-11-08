// PS5Term - GPL-3.0-or-later.
#include "builtin_shell.hpp"
#include <cassert>
#include <chrono>
#include <iterator>
#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
namespace fs = std::filesystem;
unsigned descriptors()
{
    unsigned count = 0;
    for (int i = 0; i < 1024; ++i)
        if (fcntl(i, F_GETFD) >= 0)
            ++count;
    return count;
}
std::string drain(term::BuiltinShell &shell)
{
    std::string out;
    char bytes[73];
    for (unsigned i = 0; i < 10000; ++i)
    {
        shell.tick();
        while (auto n = shell.read(bytes, sizeof bytes))
            out.append(bytes, n);
        if (!shell.busy())
            return out;
    }
    assert(false);
    return out;
}
std::string run(term::BuiltinShell &shell, std::string_view command)
{
    assert(shell.submit(command));
    return drain(shell);
}
int main()
{
    char pattern[] = "/tmp/ps5term-shell-XXXXXX";
    char *root = mkdtemp(pattern);
    assert(root);
    fs::path directory(root);
    fs::create_directory(directory / "child");
    std::string large(20000, 'q');
    {
        std::ofstream file(directory / "file name");
        file << large;
    }
    term::BuiltinShell a(root), b(root);
    auto original = fs::current_path();
    assert(run(a, "pwd").find(root) != std::string::npos);
    assert(run(a, "cd child").find("cd:") == std::string::npos);
    assert(run(a, "pwd").find(std::string(root) + "/child") != std::string::npos);
    assert(run(b, "pwd").find(std::string(root) + "/child") == std::string::npos);
    assert(fs::current_path() == original);
    assert(run(a, "cd ..").find("cd:") == std::string::npos);
    assert(run(a, "echo 'a b' \"c d\"").starts_with("a b c d\n"));
    // Pipes now work: echo hello | cat should pass through.
    assert(run(a, "echo hello | cat").starts_with("hello\n"));
    assert(run(a, "echo 'unfinished").find("syntax") != std::string::npos);
    assert(run(a, "not-a-command").find("Unknown command") != std::string::npos);
    assert(!a.submit(std::string(513, 'x')));
    assert(!a.submit(std::string_view("echo x\0bad", 10)));
    assert(run(a, "cat 'file name'").starts_with(large));
    assert(a.submit("cat 'file name'"));
    a.tick();
    for (unsigned i = 0; i < 30; ++i)
        a.tick();
    auto streamed = drain(a);
    assert(streamed.starts_with(large) &&
           streamed.size() == large.size() + 1 + std::string(root).size() + 2);
    auto listing = run(a, "ls");
    assert(listing.find("file name\n") != std::string::npos);
    assert(listing.find("child\n") != std::string::npos);
    assert(run(a, "cat child").find("Is a directory") != std::string::npos);
    assert(run(a, "cat missing").find("No such file") != std::string::npos);
    assert(a.submit("cat 'file name'"));
    assert(!a.submit("pwd"));
    a.tick();
    a.tick();
    assert(a.interrupt());
    assert(drain(a).size() < large.size());
    assert(!a.interrupt());
    assert(run(a, "id").find("uid=") != std::string::npos);
    assert(run(a, "help").find("cmd1 | cmd2") != std::string::npos);
    // Pending cancellation must not execute the command; queued output must
    // drain before another submission is accepted.
    assert(a.submit("cd child"));
    assert(a.interrupt());
    assert(!a.submit("pwd"));
    drain(a);
    assert(run(a, "pwd").find(std::string(root) + "/child") == std::string::npos);
    auto before = descriptors();
    for (unsigned i = 0; i < 32; ++i)
    {
        assert(a.submit("ls"));
        a.tick();
        assert(a.interrupt());
        drain(a);
    }
    assert(descriptors() == before);
    // Ordinary path operands preserve the filesystem's symlink/.. semantics;
    // cd retains a per-session logical cwd, like a shell's default cd -L.
    fs::create_directories(directory / "one" / "two");
    fs::create_directory_symlink(directory / "one" / "two", directory / "link");
    {
        std::ofstream file(directory / "one" / "marker");
        file << "actual-parent";
    }
    assert(run(a, "cat link/../marker").starts_with("actual-parent"));
    assert(run(a, "cd link").find("cd:") == std::string::npos);
    assert(run(a, "cd ..").find("cd:") == std::string::npos);
    assert(run(a, "pwd").starts_with(std::string(root) + "\n"));
    // Refuse special files without opening a FIFO that could block the UI.
    assert(mkfifo((directory / "fifo").c_str(), 0600) == 0);
    assert(run(a, "cat fifo").find("cat: fifo:") != std::string::npos);
    assert(a.submit("ls"));
    a.tick();
    a.reset(root);
    assert(!a.busy() && descriptors() == before);
    // Non-interactive mode.
    term::BuiltinShell command(root, false);
    assert(run(command, "echo clean") == "clean\n");
    assert(command.exit_status() == 0);
    assert(run(command, "unknown").empty());
    assert(command.exit_status() == 127);
    char errors[256]{};
    auto count = command.read_error(errors, sizeof errors);
    assert(std::string_view(errors, count).find("Unknown command") != std::string_view::npos);
    assert(run(command, "cat missing").empty());
    assert(command.exit_status() == 1 && command.read_error(errors, sizeof errors) > 0);
    // Pipes work in non-interactive mode too.
    assert(run(command, "echo piped | cat") == "piped\n");
    assert(command.exit_status() == 0);
    // Unsupported syntax still errors.
    assert(run(command, "echo x & cat").empty());
    assert(command.exit_status() == 2 && command.read_error(errors, sizeof errors) > 0);
    assert(command.submit("cat \"file name\""));
    command.tick();
    assert(command.interrupt());
    drain(command);
    assert(command.exit_status() == 130);
    // ---- Pipe and redirection tests ----
    {
        std::ofstream f(directory / "lines.txt");
        f << "cherry\napple\nbanana\napple\n";
    }
    // Pipe: ls | grep child
    auto result = run(a, "ls | grep child");
    assert(result.find("child") != std::string::npos);
    assert(result.find("file name") == std::string::npos);
    // Multi-stage pipe: echo content | sort | head
    {
        std::ofstream f(directory / "nums.txt");
        f << "3\n1\n2\n";
    }
    result = run(a, "cat nums.txt | sort | head -n 2");
    assert(result.find("1\n2\n") != std::string::npos);
    // Output redirect: echo > file
    run(a, "echo redirected > out.txt");
    result = run(a, "cat out.txt");
    assert(result.find("redirected") != std::string::npos);
    // Append redirect
    run(a, "echo line2 >> out.txt");
    result = run(a, "cat out.txt");
    assert(result.find("redirected") != std::string::npos);
    assert(result.find("line2") != std::string::npos);
    // Input redirect
    result = run(a, "head -n 1 < lines.txt");
    assert(result.find("cherry") != std::string::npos);
    // ---- Environment variable tests ----
    // Redirection belongs to the final stage, including across streaming stages.
    run(a, "echo composed | cat > composed.txt");
    assert(run(a, "cat composed.txt").starts_with("composed\n"));
    run(a, "echo appended | cat >> composed.txt");
    assert(run(a, "cat composed.txt").starts_with("composed\nappended\n"));
    run(a, "cat lines.txt | sort | head -n 2 > composed.txt");
    assert(run(a, "cat composed.txt").starts_with("apple\napple\n"));
    // A bounded pipeline must fail visibly, never process a truncated prefix.
    {
        std::ofstream file(directory / "too big");
        file << std::string(term::BuiltinShell::text_limit + 1, 'q');
    }
    assert(run(a, "cat 'file name' | wc -c").find("20000") != std::string::npos);
    result = run(a, "cat 'too big' | wc -c");
    assert(a.exit_status() != 0);
    assert(result.find("limit") != std::string::npos);
    assert(descriptors() == before);
    run(a, "export FOO=bar");
    result = run(a, "echo $FOO");
    assert(result.find("bar") != std::string::npos);
    result = run(a, "env");
    assert(result.find("FOO=bar") != std::string::npos);
    run(a, "unset FOO");
    result = run(a, "env");
    assert(result.find("FOO") == std::string::npos);
    // $? expansion
    run(a, "true");
    result = run(a, "echo $?");
    assert(result.find("0") != std::string::npos);
    run(a, "false");
    result = run(a, "echo $?");
    assert(result.find("1") != std::string::npos);
    // ---- Text filter tests ----
    // grep
    result = run(a, "grep apple lines.txt");
    assert(result.find("apple") != std::string::npos);
    assert(result.find("cherry") == std::string::npos);
    // grep -c
    result = run(a, "grep -c apple lines.txt");
    assert(result.find("2") != std::string::npos);
    // grep -v
    result = run(a, "grep -v apple lines.txt");
    assert(result.find("cherry") != std::string::npos);
    assert(result.find("banana") != std::string::npos);
    // grep -i
    {
        std::ofstream f(directory / "mixed.txt");
        f << "Hello World\nhello there\nGoodbye\n";
    }
    result = run(a, "grep -i hello mixed.txt");
    assert(result.find("Hello World") != std::string::npos);
    assert(result.find("hello there") != std::string::npos);
    // grep -n
    result = run(a, "grep -n apple lines.txt");
    assert(result.find("2:apple") != std::string::npos);
    assert(result.find("4:apple") != std::string::npos);
    // head
    result = run(a, "head -n 2 lines.txt");
    assert(result.find("cherry") != std::string::npos);
    assert(result.find("apple") != std::string::npos);
    assert(result.find("banana") == std::string::npos);
    // tail
    result = run(a, "tail -n 2 lines.txt");
    assert(result.find("banana") != std::string::npos);
    assert(result.find("cherry") == std::string::npos);
    // wc
    result = run(a, "wc lines.txt");
    assert(result.find("4") != std::string::npos); // 4 lines
    // sort
    result = run(a, "sort lines.txt");
    assert(result.starts_with("apple\n"));
    // sort -r
    result = run(a, "sort -r lines.txt");
    assert(result.starts_with("cherry\n"));
    // sort -n
    result = run(a, "sort -n nums.txt");
    assert(result.starts_with("1\n"));
    // uniq
    result = run(a, "sort lines.txt | uniq");
    assert(result.find("apple\napple") == std::string::npos);
    // uniq -c
    result = run(a, "sort lines.txt | uniq -c");
    assert(result.find("2 apple") != std::string::npos);
    // tee
    result = run(a, "echo tee-test | tee tee-out.txt");
    assert(result.find("tee-test") != std::string::npos);
    result = run(a, "cat tee-out.txt");
    assert(result.find("tee-test") != std::string::npos);
    // ---- File operation tests ----
    // mkdir
    run(a, "mkdir newdir");
    assert(fs::is_directory(directory / "newdir"));
    // mkdir -p
    run(a, "mkdir -p deep/nested/dir");
    assert(fs::is_directory(directory / "deep" / "nested" / "dir"));
    // touch
    run(a, "touch newfile");
    assert(fs::exists(directory / "newfile"));
    // cp
    run(a, "cp lines.txt copy.txt");
    result = run(a, "cat copy.txt");
    assert(result.find("cherry") != std::string::npos);
    // mv
    run(a, "mv copy.txt moved.txt");
    assert(!fs::exists(directory / "copy.txt"));
    assert(fs::exists(directory / "moved.txt"));
    // rm
    run(a, "rm moved.txt");
    assert(!fs::exists(directory / "moved.txt"));
    // rmdir
    run(a, "rmdir newdir");
    assert(!fs::exists(directory / "newdir"));
    // chmod
    run(a, "chmod 0755 newfile");
    auto perms = fs::status(directory / "newfile").permissions();
    assert((perms & fs::perms::owner_exec) != fs::perms::none);
    // stat
    result = run(a, "stat newfile");
    assert(result.find("regular") != std::string::npos);
    assert(result.find("Mode") != std::string::npos);
    // ---- System info tests ----
    result = run(a, "uname");
    assert(result.find('\n') != std::string::npos && result[0] != '\n');
    result = run(a, "uname -a");
    assert(result.size() > 10);
    result = run(a, "hostname");
    assert(!result.empty());
    result = run(a, "whoami");
    assert(result.find("uid") != std::string::npos);
    result = run(a, "date");
    assert(result.find("20") != std::string::npos); // year
    // ---- Path utility tests ----
    result = run(a, "basename /foo/bar/baz");
    assert(result.starts_with("baz\n"));
    result = run(a, "dirname /foo/bar/baz");
    assert(result.starts_with("/foo/bar\n"));
    // ---- Misc ----
    assert(run(a, "true").find(root) != std::string::npos); // just prompt
    assert(a.exit_status() == 0);
    run(a, "false");
    assert(a.exit_status() == 1);
    result = run(a, "echo -n hello");
    assert(result.starts_with("hello" + std::string(root)));
    // clear sends escape sequence
    result = run(a, "clear");
    assert(result.find("\033[2J") != std::string::npos);
    // Pipe with streaming first stage: cat lines | head
    result = run(a, "cat lines.txt | head -n 1");
    assert(result.find("cherry") != std::string::npos);
    assert(result.find("apple") == std::string::npos);
    // ---- Reproduced defects from the 2026-11-04 review ----
    fs::create_directory(directory / "r");
    run(a, std::string("cd ") + root + "/r");
    const auto slurp = [&](const char *name)
    {
        std::ifstream f(directory / "r" / name, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), {});
    };
    const auto exists = [&](const char *name) { return fs::exists(directory / "r" / name); };
    const auto put = [&](const char *name, const std::string &text)
    {
        std::ofstream f(directory / "r" / name, std::ios::binary);
        f << text;
    };
    // 1. A syntax error anywhere runs nothing, including commands before it.
    put("victim", "x");
    run(a, "echo a | rm victim |");
    assert(a.exit_status() == 2 && exists("victim"));
    run(a, "rm victim ; echo a |");
    assert(a.exit_status() == 2 && exists("victim"));
    // 2. cp onto itself keeps the file; into a directory keeps the name.
    put("same", "keep\n");
    result = run(a, "cp same ./same");
    assert(a.exit_status() == 1 && slurp("same") == "keep\n" &&
           result.find("same file") != std::string::npos);
    fs::create_directory(directory / "r" / "d");
    run(a, "cp same d");
    assert(a.exit_status() == 0 && slurp("d/same") == "keep\n");
    run(a, "mv d/same d/moved");
    assert(!exists("d/same") && exists("d/moved"));
    // A larger copy streams and survives the old 1 MiB limit.
    put("big", std::string(3 * 1024 * 1024, 'b'));
    run(a, "cp big big2");
    assert(a.exit_status() == 0 && fs::file_size(directory / "r" / "big2") == 3 * 1024 * 1024);
    // 3. Operators touching words, and 2> as a stream rather than an argument.
    run(a, "echo hi>adj");
    assert(slurp("adj") == "hi\n");
    run(a, "echo 2 > two");
    assert(slurp("two") == "2\n");
    run(a, "cat missing 2>err");
    assert(a.exit_status() == 1 && slurp("err").find("No such file") != std::string::npos);
    run(a, "cat missing 2>/dev/null");
    assert(a.exit_status() == 1);
    run(a, "cat missing > out 2>&1");
    assert(slurp("out").find("No such file") != std::string::npos);
    // 4. Backslashes and quotes.
    put("a b", "spaced\n");
    assert(run(a, "cat a\\ b").starts_with("spaced\n"));
    run(a, "echo x > my\\ file");
    assert(slurp("my file") == "x\n" && !exists("my"));
    assert(run(a, "echo \"a\\\"b\"").starts_with("a\"b\n"));
    // 5. A variable's value is data, never syntax.
    run(a, "Q='> inj'");
    run(a, "echo hi $Q");
    assert(!exists("inj"));
    run(a, "Q='say \"hi\"'");
    assert(run(a, "echo \"$Q\"").starts_with("say \"hi\"\n"));
    // 6. FIFOs fail instead of freezing the UI thread.
    assert(!mkfifo((directory / "r" / "pipe").c_str(), 0600));
    for (const char *line : {"head pipe", "cat < pipe", "echo x > pipe", "echo x | tee pipe"})
    {
        run(a, line);
        assert(a.exit_status() != 0);
    }
    run(a, "rm pipe");
    assert(!exists("pipe"));
    // 8. Large inputs are complete, not silently capped.
    std::string many;
    for (int i = 0; i < 1500; ++i)
        many += "L" + std::to_string(i) + "\n";
    put("many", many);
    assert(run(a, "tail -n 1 many").starts_with("L1499\n"));
    assert(run(a, "sort many | wc -l").find("1500") != std::string::npos);
    assert(run(a, "tail -n +1500 many").starts_with("L1499\n"));
    assert(run(a, "head -2 many").starts_with("L0\nL1\n"));
    // 9. uniq adds no phantom line.
    put("dups", "a\na\nb\n");
    assert(run(a, "uniq dups").starts_with("a\nb\n/"));
    // 10. Errors reach the terminal, not the pipe or file.
    result = run(a, "cat missing > captured");
    assert(result.find("No such file") != std::string::npos && slurp("captured").empty());
    assert(run(a, "cat missing | wc -l").find("No such file") != std::string::npos);
    // 11. An empty pipe is ordinary input.
    result = run(a, "grep zzz dups | wc -l");
    assert(a.exit_status() == 0 && result.find("0") != std::string::npos);
    put("cleared", "old");
    run(a, "true | tee cleared");
    assert(a.exit_status() == 0 && slurp("cleared").empty());
    // 12. A pipeline's status is its last command's.
    run(a, "false | echo hi");
    assert(a.exit_status() == 0);
    // 14. Output larger than one read arrives whole.
    std::string lines5k;
    for (int i = 0; i < 2000; ++i)
        lines5k += "abc\n";
    put("five", lines5k);
    result = run(a, "grep a five");
    assert(a.exit_status() == 0 && result.starts_with(lines5k));
    put("longline", std::string(4090, 'z') + "\n");
    assert(run(a, "head -n 1 longline").starts_with(std::string(4090, 'z') + "\n"));
    // 15. Extra operands act or refuse, never get ignored.
    run(a, "mkdir m1 m2");
    assert(exists("m1") && exists("m2"));
    run(a, "sort dups missing");
    assert(a.exit_status() != 0);
    // 16. A bare redirect still creates the file.
    run(a, "> bare");
    assert(exists("bare"));
    // 17. rm removes what lstat cannot describe, such as a dangling link.
    assert(!symlink("nowhere", (directory / "r" / "dangling").c_str()));
    run(a, "rm dangling");
    assert(a.exit_status() == 0 && !fs::is_symlink(directory / "r" / "dangling"));
    // 19. cat f >> f stops at once.
    put("loop", "data\n");
    run(a, "cat loop >> loop");
    assert(a.exit_status() == 1 && slurp("loop") == "data\n");
    // 20-21. Overlong expansion refuses; literal dollars survive; ${?} works.
    run(a, "V=" + std::string(100, 'A'));
    run(a, "echo $V$V$V$V$V$V$V$V$V$V$V");
    assert(a.exit_status() == 2);
    assert(run(a, "echo \"5$\"").starts_with("5$\n"));
    run(a, "true");
    assert(run(a, "echo ${?}").starts_with("0\n"));
    run(a, "echo $(pwd)");
    assert(a.exit_status() == 2);
    // 22-26.
    put("old", "x");
    fs::last_write_time(directory / "r" / "old",
                        fs::file_time_type::clock::now() - std::chrono::hours(48));
    const auto stale = fs::last_write_time(directory / "r" / "old");
    run(a, "touch old");
    assert(fs::last_write_time(directory / "r" / "old") > stale);
    run(a, "mkdir -p old");
    assert(a.exit_status() != 0);
    run(a, "chmod '' old");
    assert(a.exit_status() == 2);
    run(a, "chmod 200000000 old");
    assert(a.exit_status() == 2);
    put("nums", "99999999999999999999\n-1\n5\n");
    assert(run(a, "sort -n nums").starts_with("-1\n5\n99999"));
    run(a, "rmdir ''");
    assert(a.exit_status() != 0 && fs::exists(directory / "r"));
    // 32.
    assert(run(a, "head -n 4294967297 dups").starts_with("a\na\nb\n"));
    assert(run(a, "basename /").starts_with("/\n"));
    // ---- Command lists ----
    assert(run(a, "echo one ; echo two").starts_with("one\ntwo\n"));
    assert(run(a, "true && echo yes").starts_with("yes\n"));
    assert(run(a, "false && echo no ; echo after").starts_with("after\n"));
    assert(run(a, "false || echo rescued").starts_with("rescued\n"));
    assert(run(a, "true || echo no ; echo after").starts_with("after\n"));
    assert(run(a, "false && echo a || echo b").starts_with("b\n"));
    assert(run(a, "X=1 ; echo $X").starts_with("1\n"));
    assert(run(a, "echo 'a;b && c'").starts_with("a;b && c\n"));
    run(a, "echo a &");
    assert(a.exit_status() == 2);
    run(a, "; echo a");
    assert(a.exit_status() == 2);
    run(a, "echo a &&");
    assert(a.exit_status() == 2);
    assert(run(a, "echo end ;").starts_with("end\n"));
    // ---- Navigation and environment ----
    const std::string here = run(a, "pwd");
    run(a, "cd m1");
    assert(run(a, "cd -").starts_with(std::string(root) + "/r\n"));
    assert(run(a, "echo $PWD").starts_with(std::string(root) + "/r\n"));
    assert(run(a, "echo $HOME").starts_with("/data\n"));
    // ---- ls ----
    put(".hidden", "x");
    put("zed", "12345");
    result = run(a, "ls");
    assert(result.find(".hidden") == std::string::npos &&
           result.find("bare\n") < result.find("zed\n"));
    assert(run(a, "ls -a").find(".hidden\n") != std::string::npos);
    result = run(a, "ls -l zed");
    assert(result.find("-rw") != std::string::npos && result.find(" 5  ") != std::string::npos);
    result = run(a, "ls m1 m2");
    assert(result.find("m1:\n") != std::string::npos && result.find("m2:\n") != std::string::npos);
    run(a, "ls missing");
    assert(a.exit_status() == 1);
    // A directory far larger than one sorted batch still lists every name.
    fs::create_directory(directory / "r" / "crowd");
    for (int i = 0; i < 1500; ++i)
        std::ofstream(directory / "r" / "crowd" / ("f" + std::to_string(i)));
    assert(run(a, "ls crowd | wc -l").find("1500") != std::string::npos);
    // ---- New tools ----
    put("abc", "abc");
    assert(run(a, "sha256sum abc")
               .starts_with(
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  abc\n"));
    put("million", std::string(1000000, 'a'));
    assert(run(a, "sha256sum million")
               .starts_with("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    assert(run(a, "xxd abc").starts_with("00000000: 6162 63"));
    assert(run(a, "printf 'a%sc\\n' b").starts_with("abc\n"));
    assert(run(a, "sleep 0").find("sleep:") == std::string::npos && a.exit_status() == 0);
    {
        assert(a.submit("sleep 60"));
        a.tick();
        assert(a.busy() && a.interrupt());
        drain(a);
        assert(a.exit_status() == 130);
    }
    run(a, "uname -a");
    assert(a.exit_status() == 0);
    // Long commands stay interruptible and leave nothing behind.
    {
        assert(a.submit("cp big big3"));
        a.tick();
        assert(a.busy() && a.interrupt());
        drain(a);
        assert(a.exit_status() == 130);
    }
    // Native storage summaries count allocated blocks, not logical file sizes.
    {
        fs::path usage = directory / "r" / "usage";
        fs::create_directory(usage);
        fs::create_directory(usage / "nested");
        std::ofstream(usage / ".hidden") << "hidden";
        std::ofstream(usage / "nested" / "file") << "payload";
        fs::create_hard_link(usage / "nested" / "file", usage / "hard");
        fs::create_directory_symlink(usage, usage / "loop");
        fs::create_symlink("missing", usage / "dangling");
        assert(mkfifo((usage / "fifo").c_str(), 0600) == 0);
        int sparse = open((usage / "sparse").c_str(), O_CREAT | O_RDWR, 0600);
        assert(sparse >= 0 && ftruncate(sparse, 1024 * 1024 * 1024) == 0);
        assert(close(sparse) == 0);
        std::uint64_t blocks = 0;
        for (const auto &p : {usage, usage / "nested", usage / ".hidden", usage / "nested" / "file",
                              usage / "loop", usage / "dangling", usage / "fifo", usage / "sparse"})
        {
            struct stat st
            {
            };
            assert(lstat(p.c_str(), &st) == 0);
            blocks += st.st_blocks;
        }
        const auto expected = std::to_string((blocks + 1) / 2) + "\tusage\n";
        assert(run(a, "du -sk usage").starts_with(expected));
        assert(a.exit_status() == 0);
        assert(run(a, "du usage | cat").starts_with(expected));
        assert(run(a, "du -h usage").find("\tusage\n") != std::string::npos);
        run(a, "cd usage");
        assert(run(a, "du").starts_with(std::to_string((blocks + 1) / 2) + "\t.\n"));
        run(a, "cd ..");
        assert(run(a, "du -s usage/missing").find("No such file") != std::string::npos);
        assert(a.exit_status() == 1);
        assert(run(a, "du -z usage").find("du [-shk]") != std::string::npos);
        assert(a.exit_status() == 2);
        assert(run(a, "du usage child").find("du [-shk]") != std::string::npos);
        assert(a.exit_status() == 2);
        // Opening traversal must remain cancellable and release its descriptors.
        assert(a.submit("du usage"));
        a.tick();
        assert(a.busy() && a.interrupt());
        drain(a);
        assert(a.exit_status() == 130);
        assert(descriptors() == before);
        fs::remove_all(usage);
    }
    // Native filename patterns preserve quoting and never reinterpret filenames.
    {
        const auto glob = directory / "r" / "glob";
        fs::create_directory(glob);
        for (const char *name : {"b.log", "a.log", ".hidden.log", "a space.log", "a;echo BAD.log",
                                 "literal*.txt", "q1.txt", "q2.txt"})
            std::ofstream(glob / name) << name;
        assert(run(a, "echo glob/q?.txt").starts_with("glob/q1.txt glob/q2.txt\n"));
        assert(run(a, "echo glob/*.log")
                   .starts_with("glob/a space.log glob/a.log glob/a;echo BAD.log glob/b.log\n"));
        assert(run(a, "echo 'glob/*.log' glob/\\*.log \"glob/*.log\"")
                   .starts_with("glob/*.log glob/*.log glob/*.log\n"));
        assert(run(a, "echo glob/'literal*'.t?t").starts_with("glob/literal*.txt\n"));
        assert(run(a, "echo glob/.*.log").starts_with("glob/.hidden.log\n"));
        assert(run(a, "echo glob/no-match*.txt").starts_with("glob/no-match*.txt\n"));
        assert(run(a, "echo missing/*.txt").starts_with("missing/*.txt\n"));
        assert(run(a, "PAT='glob/q?.txt'; echo $PAT").starts_with("glob/q1.txt glob/q2.txt\n"));
        assert(run(a, "echo \"$PAT\"").starts_with("glob/q?.txt\n"));
        assert(run(a, "echo glob/q?.txt | wc -w").starts_with("      2\n"));
        assert(run(a, "echo ok | cat glob/q?.txt").starts_with("q1.txtq2.txt"));
        const auto failed_redirect = glob / "must-not-exist";
        assert(run(a, "echo g*/q?.txt > glob/must-not-exist").find("wildcard") !=
               std::string::npos);
        assert(a.exit_status() == 2 && !fs::exists(failed_redirect));
        assert(run(a, "echo glob/[ab].log > glob/must-not-exist").find("wildcard") !=
               std::string::npos);
        assert(a.exit_status() == 2 && !fs::exists(failed_redirect));
        assert(run(a, "echo ok > glob/*.out").find("wildcard") != std::string::npos);
        assert(a.exit_status() == 2 && !fs::exists(glob / "*.out"));
        assert(a.submit("echo glob/*.log"));
        a.tick();
        assert(a.busy() && a.interrupt());
        drain(a);
        assert(a.exit_status() == 130 && descriptors() == before);
        for (int i = 0; i < 40; ++i)
            std::ofstream(glob / ("limit" + std::to_string(i)));
        assert(a.submit("echo glob/never-match*"));
        a.tick();
        a.tick();
        assert(a.busy() && a.interrupt());
        drain(a);
        assert(a.exit_status() == 130 && descriptors() == before);
        assert(run(a, "echo glob/limit* > glob/must-not-exist").find("wildcard") !=
               std::string::npos);
        assert(a.exit_status() == 2 && !fs::exists(failed_redirect));
        fs::remove_all(glob);
    }
    // Verify descriptor hygiene after all the pipe/redirect operations
    assert(descriptors() == before);
    fs::remove_all(directory);
    std::puts("builtin shell tests passed");
}
