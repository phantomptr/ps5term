// PS5Term - host rendering of the actual app scene. SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>
static std::size_t offset(unsigned x, unsigned y)
{
    auto value = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                 ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                 ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    return ((static_cast<std::size_t>((y >> 7) * 15 + (x >> 7))) << 16) + value;
}
int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    std::vector<std::uint32_t> pixels(16 * 1024 * 1024 / 4);
    term::app.welcome();
    std::string_view scene = argc > 2 ? argv[2] : "";
    // Mouse scenes drive the real App::mouse path with relative samples.
    std::uint64_t now = 10000000;
    int x = 960, y = 540;
    auto sample = [&](int tx, int ty, std::uint32_t buttons)
    {
        term::MouseSample s;
        s.connected = true;
        s.buttons = buttons;
        s.dx = tx - x;
        s.dy = ty - y;
        x = tx;
        y = ty;
        term::app.mouse(s, now += 1000);
    };
    if (scene == "mouse")
    {
        term::app.key(term::Key::character, 'l');
        term::app.key(term::Key::character, 's');
        for (const char *line :
             {"$ ls -la /data/ps5term\n", "drwx------  root  512 ps5term\n",
              "-rw-r--r--  root   4  mouse.speed\n", "-rw-r--r--  root  91  ssh.enabled\n",
              "$ uname -a\n", "FreeBSD ps5 11.0-RELEASE amd64\n"})
            term::app.feed(line);
        sample(96 + 12 * 16, 226 + 2 * 24 + 4, 0); // drag-select two lines
        sample(96 + 12 * 16, 226 + 2 * 24 + 4, 1);
        sample(96 + 30 * 16, 226 + 3 * 24 + 4, 1);
        sample(96 + 30 * 16, 226 + 3 * 24 + 4, 0);
        sample(96 + 30 * 16, 226 + 3 * 24 + 4, 2); // right-click menu
        sample(96 + 30 * 16, 226 + 3 * 24 + 4, 0);
        sample(96 + 30 * 16 + 40, 226 + 3 * 24 + 22, 0); // hover Copy
    }
    else if (scene == "keyboard")
    {
        term::app.controller(0x1000, 0x1000);
        sample(84 + 147 * 4 + 60, 678 + 37 + 12, 0);
    }
    else if (!scene.empty())
    {
        term::app.key(term::Key::settings);
        if (scene == "settings-mouse")
            sample(1700, 850, 0);
        if (scene == "about")
            term::app.key(term::Key::function, 1); // F1 in Settings
    }
    ps5::demo::Canvas canvas(pixels.data());
    term::app.draw(canvas);
    FILE *file = std::fopen(argv[1], "wb");
    if (!file)
        return 1;
    std::fprintf(file, "P6\n1920 1080\n255\n");
    auto *bytes = reinterpret_cast<const unsigned char *>(pixels.data());
    for (unsigned y = 0; y < 1080; y++)
        for (unsigned x = 0; x < 1920; x++)
            std::fwrite(bytes + offset(x, y), 1, 3, file);
    return std::fclose(file) == 0 ? 0 : 1;
}
