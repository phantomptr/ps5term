// PS5Term - native input ABI based on ProsperoStore / ps5-homebrew-ui.
// Copyright (C) 2026 BlackBearReloaded; PS5Term contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "input.hpp"
#include <cstddef>
namespace
{
struct PadSample
{
    std::uint32_t buttons;
    std::uint8_t sticks[6];
    std::uint8_t reserved[66];
    std::int32_t connected;
    std::uint64_t timestamp;
    std::uint8_t tail[32];
};
static_assert(sizeof(PadSample) == 120);
struct KeyboardSample
{
    std::uint64_t timestamp;
    std::uint8_t intercepted;
    std::uint8_t pad0[7];
    std::uint8_t connected;
    std::uint8_t pad1[3];
    std::int32_t length;
    std::uint32_t leds;
    std::uint32_t modifiers;
    std::uint16_t keys[16];
    std::uint8_t tail[32];
};
static_assert(sizeof(KeyboardSample) == 96);
} // namespace
extern "C"
{
    int sceUserServiceInitialize(const void *);
    int sceUserServiceGetInitialUser(int *);
    int scePadInit();
    int scePadOpen(int, int, int, const void *);
    int scePadRead(int, PadSample *, int);
    int sceSysmoduleLoadModule(std::uint16_t);
    int sceKeyboardInit();
    int sceKeyboardOpen(int, int, int, const void *);
    int sceKeyboardRead(int, void *, int);
}
namespace term
{
void Input::key(unsigned k, unsigned mods, KeyHandler cb) noexcept
{
    translate_key(k, mods, caps_, cb);
}
void Input::tick(std::uint64_t now, KeyHandler cb) noexcept
{
    if (!initialized_)
    {
        sceUserServiceInitialize(nullptr);
        sceUserServiceGetInitialUser(&user_);
        scePadInit();
        sceSysmoduleLoadModule(0x0106);
        sceKeyboardInit();
        initialized_ = true;
    }
    if (now >= scan_)
    {
        scan_ = now + 2000000;
        if (user_ < 0)
            sceUserServiceGetInitialUser(&user_);
        if (user_ >= 0 && pad_ < 0)
            pad_ = scePadOpen(user_, 0, 0, nullptr);
        std::uint64_t params = 0;
        if (user_ >= 0 && keyboard_ < 0)
            keyboard_ = sceKeyboardOpen(user_, 0, 0, &params);
    }
    pressed_ = 0;
    if (pad_ >= 0)
    {
        PadSample samples[16]{};
        int n = scePadRead(pad_, samples, 16);
        for (int i = 0; i < n && i < 16; ++i)
        {
            auto b = samples[i].connected ? samples[i].buttons : 0;
            pressed_ |= b & ~buttons_;
            buttons_ = b;
        }
    }
    // Only navigation and delete repeat. Holding Run, SSH toggle, or Settings
    // must never trigger another action.
    const auto repeating = buttons_ & (0x00f0U | 0x8000U);
    if (repeating != repeat_buttons_)
    {
        repeat_buttons_ = repeating;
        pad_repeat_at_ = now + 400000;
    }
    else if (repeating && now >= pad_repeat_at_)
    {
        pressed_ |= repeating;
        pad_repeat_at_ = now + 90000;
    }
    if (keyboard_ < 0)
        return;
    KeyboardSample samples[16]{};
    int n = sceKeyboardRead(keyboard_, samples, 16);
    for (int i = 0; i < n && i < 16; ++i)
    {
        auto &s = samples[i];
        if (raw_keyboard)
            raw_keyboard(s.modifiers, s.keys, 16, s.connected, s.intercepted);
        modifiers_ = s.connected && !s.intercepted ? s.modifiers : 0;
        if (!s.connected || s.intercepted)
        {
            previous_ = {};
            repeat_key_ = 0;
            continue;
        }
        for (auto k : s.keys)
        {
            if (!k)
                continue;
            bool held = false;
            for (auto old : previous_)
                if (old == k)
                    held = true;
            if (!held)
            {
                key(k, s.modifiers, cb);
                repeat_key_ = k;
                repeat_at_ = now + 450000;
            }
        }
        for (unsigned j = 0; j < 16; ++j)
            previous_[j] = s.keys[j];
    }
    // Outside the sample loop: a device that only reports changes must still repeat.
    bool still = false;
    for (auto k : previous_)
        if (k == repeat_key_)
            still = true;
    if (still && repeat_key_ && !(repeat_key_ >= 57 && repeat_key_ <= 69) && repeat_key_ != 40 &&
        repeat_key_ != 41 && repeat_key_ != 88 && now >= repeat_at_)
    {
        key(repeat_key_, modifiers_, cb);
        repeat_at_ = now + 45000;
    }
}
} // namespace term
