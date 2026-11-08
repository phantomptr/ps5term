// PS5Term - platform mouse (libSceMouse). GPL-3.0-or-later.
//
// Linked directly like the keyboard in input.cpp: the import stub is generated
// at build time (tools/make-stub.sh, called by tools/build.sh) because the
// pinned SDK ships none, and the module is loaded with
// sceSysmoduleLoadModule(0x00a9) before the first call.
#pragma once
#include <cstdint>
namespace term
{
// One SceMouseData sample: relative motion since the previous sample.
struct MouseSample
{
    std::int32_t dx = 0, dy = 0, wheel = 0;
    std::uint32_t buttons = 0; // 0x1 left, 0x2 right, 0x4 middle
    bool connected = false;
};
class Mouse
{
  public:
    // Opens (and reopens) the initial user's mouse as needed and returns up
    // to max samples read since the last call.
    int poll(std::uint64_t now, MouseSample *out, int max) noexcept;
    bool connected() const noexcept
    {
        return connected_;
    }
    // Last sceSysmoduleLoadModule / sceMouseOpen result, for diagnostics.
    int status() const noexcept
    {
        return status_;
    }
    int handle() const noexcept
    {
        return handle_;
    }

  private:
    bool loaded_ = false, reopen_ = false, connected_ = false;
    int handle_ = -1, user_ = -1, status_ = 0, load_attempts_ = 0;
    std::uint64_t next_open_ = 0;
    void close() noexcept;
};
} // namespace term
