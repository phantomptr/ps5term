// PS5Term - platform mouse (libSceMouse). GPL-3.0-or-later.
//
// Call sequence measured on FW 13.60 with the research monitor
// (src/research.cpp): sceSysmoduleLoadModule(0x00a9) -> sceMouseInit ->
// sceMouseOpen(user, 0, 0, param) -> sceMouseRead. The first open right after
// launch can fail (0x809b0001 while the initial user is not known yet) and
// succeeds on a retry, so opening is retried every 2 s. An open handle keeps
// reporting devices that come and go; it is reopened once after a disconnect
// (a device that a killed process left behind can hold the first slot), when
// the user changes, and after a read error.
#include "mouse.hpp"
extern "C"
{
    int sceSysmoduleLoadModule(std::uint16_t id);
    int sceUserServiceGetInitialUser(int *user);
    int sceMouseInit();
    int sceMouseOpen(int user, int type, int index, const void *param);
    int sceMouseRead(int handle, void *data, int count);
    int sceMouseClose(int handle);
}
namespace term
{
namespace
{
constexpr std::uint16_t sysmodule_mouse = 0x00a9;
constexpr std::uint64_t retry_us = 2000000;
struct MouseData // SceMouseData, 40 bytes
{
    std::uint64_t timestamp;
    std::uint8_t connected;
    std::uint8_t pad0[3];
    std::uint32_t buttons;
    std::int32_t x, y, wheel, tilt;
    std::uint8_t reserved[8];
};
static_assert(sizeof(MouseData) == 40);
} // namespace

void Mouse::close() noexcept
{
    if (handle_ >= 0)
        (void)sceMouseClose(handle_);
    handle_ = -1;
    connected_ = false;
}

int Mouse::poll(std::uint64_t now, MouseSample *out, int max) noexcept
{
    if (now >= next_open_)
    {
        next_open_ = now + retry_us;
        if (!loaded_)
        {
            // A module that will not load is retried a few times, then left.
            if (load_attempts_ >= 5)
                return 0;
            ++load_attempts_;
            status_ = sceSysmoduleLoadModule(sysmodule_mouse);
            if (status_ < 0)
                return 0;
            status_ = sceMouseInit();
            if (status_ < 0)
                return 0;
            loaded_ = true;
        }
        int user = -1;
        sceUserServiceGetInitialUser(&user);
        if (handle_ >= 0 && (reopen_ || user != user_))
        {
            close();
            reopen_ = false;
        }
        user_ = user;
        if (handle_ < 0 && user_ >= 0)
        {
            std::uint8_t param[8]{};
            status_ = sceMouseOpen(user_, 0, 0, param);
            if (status_ >= 0)
                handle_ = status_;
        }
    }
    if (handle_ < 0)
        return 0;
    MouseData data[16];
    int n = sceMouseRead(handle_, data, max < 16 ? max : 16);
    if (n < 0)
    {
        status_ = n;
        close();
        next_open_ = now + retry_us;
        return 0;
    }
    int count = 0;
    for (int i = 0; i < n && i < 16 && count < max; ++i)
    {
        bool connected = data[i].connected != 0;
        if (connected_ && !connected)
        {
            reopen_ = true; // once per disconnect, at the next 2 s scan
            next_open_ = now + retry_us;
        }
        connected_ = connected;
        auto &sample = out[count++];
        sample.dx = data[i].x;
        sample.dy = data[i].y;
        sample.wheel = data[i].wheel;
        sample.buttons = connected ? data[i].buttons : 0;
        sample.connected = connected;
    }
    return count;
}
} // namespace term
