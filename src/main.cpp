// PS5Term - native entry point. GPL-3.0-or-later.
#include "app.hpp"
#include <ctime>
namespace
{
void draw(ps5::demo::Canvas &canvas) noexcept
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    term::app.tick(static_cast<std::uint64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000);
    term::app.draw(canvas);
}
} // namespace
extern "C" int sceNetInit();
int main()
{
    // SceNet must be initialized before the first loopback socket is opened.
    sceNetInit();
    term::app.welcome();
    ps5::demo::run(draw, "PS5Term ready");
}
