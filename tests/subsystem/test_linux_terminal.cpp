// PS5Term - GPL-3.0-or-later. Real guest/VT bridge acceptance.
#include "linux_terminal.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using term::LinuxTerminal;
using term::TerminalModel;
static auto now()
{
    return std::chrono::steady_clock::now();
}
static std::vector<unsigned char> load(const char *directory, const char *name)
{
    std::string path = std::string(directory) + "/" + name;
    FILE *file = fopen(path.c_str(), "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size > 0 && size < 32 * 1024 * 1024);
    rewind(file);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    assert(fread(data.data(), 1, data.size(), file) == data.size());
    fclose(file);
    return data;
}
static std::string visible(const TerminalModel &model)
{
    std::string result;
    for (int row = 0; row < model.rows(); row++)
    {
        for (int col = 0; col < model.columns(); col++)
        {
            auto point = model.cell(row, col).codepoints[0];
            result += point < 128 && point ? char(point) : ' ';
        }
        result += '\n';
    }
    return result;
}
static void until(LinuxTerminal &terminal, const char *marker)
{
    auto deadline = now() + std::chrono::seconds(30);
    while (visible(terminal.model()).find(marker) == std::string::npos)
    {
        assert(terminal.healthy());
        assert(terminal.status().state != PS5_SESSION_FAILED);
        if (now() >= deadline)
        {
            fprintf(stderr, "VT timeout: %s\n%s", marker, visible(terminal.model()).c_str());
            abort();
        }
        terminal.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
static void command(LinuxTerminal &terminal, const char *text)
{
    assert(terminal.model().paste(text));
    assert(terminal.model().key(TerminalModel::Key::enter));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    auto bios = load(argv[1], "bbl64.bin"), kernel = load(argv[1], "kernel-riscv64.bin"),
         disk = load(argv[1], "root-riscv64.bin");
    Ps5LinuxImages images = {bios.data(),   bios.size(), kernel.data(),
                             kernel.size(), disk.data(), disk.size()};
    LinuxTerminal terminal(25, 100);
    assert(terminal.healthy());
    for (int iteration = 0; iteration < 2; iteration++)
    {
        assert(terminal.start(images));
        assert(!terminal.start(images));
        until(terminal, "~ #");
        assert(!terminal.resize(0, 80));
        assert(terminal.resize(31, 97));
        command(terminal, "test \"$(stty size)\" = '31 97' && printf 'VT_%s\\n' RESIZED; printf "
                          "'b\\na\\n' | sort | head -n1; printf 'VT_%s\\n' PIPELINE");
        until(terminal, "VT_RESIZED");
        until(terminal, "VT_PIPELINE");
        // Query from guest must make the round trip through the model's reply
        // queue. Home first makes its six-byte response deterministic.
        command(terminal, "stty raw -echo; printf '\\033[H\\033[6n'; dd bs=1 count=6 2>/dev/null | "
                          "od -An -tx1 > /tmp/vt-reply; stty sane; grep -q '1b 5b 31 3b 31 52' "
                          "/tmp/vt-reply && printf 'VT_%s\\n' REPLY");
        until(terminal, "VT_REPLY");
        command(terminal, "printf 'VT_%s\\n' SLEEPING; sleep 60");
        until(terminal, "VT_SLEEPING");
        assert(terminal.model().text('c', TerminalModel::control));
        command(terminal, "printf 'VT_%s\\n' INTERRUPTED");
        until(terminal, "VT_INTERRUPTED");
        assert(terminal.stop());
        assert(terminal.status().state == PS5_SESSION_STOPPED);
        assert(terminal.stop());
    }
    LinuxTerminal invalid(0, 80);
    assert(!invalid.healthy());
    assert(!invalid.start(images));
    puts("PASS: real Linux/VT model, bounded pump, resize, shell pipeline, terminal reply, Ctrl-C "
         "and clean restart");
}
