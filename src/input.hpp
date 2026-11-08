// PS5Term - platform keyboard and DualSense input. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstdint>
namespace term
{
// Character payload for F2..F6 app keys: set when the key came from a function key,
// with the arrow-key modifier bits (shift 1, alt 2, control 4) below it.
inline constexpr char from_function_key = 0x10;
enum class Key
{
    toggle_split,
    focus_pane,
    divider_left,  // Ctrl+Shift+Left: move the split divider left
    divider_right, // Ctrl+Shift+Right
    new_tab,
    close_tab,
    next_tab,
    previous_tab,
    search,      // Ctrl+Shift+F: app scrollback search
    select_text, // Ctrl+Shift+Space: start selecting output text with the keyboard
    go_tab,      // Ctrl+Shift+1..4: show that tab (ch = index)
    character,
    tab,
    backtab,
    menu,
    toggle_keyboard,
    enter,
    backspace,
    del,
    insert,
    left,
    right,
    home,
    end,
    up,
    down,
    pageup,
    pagedown,
    escape,
    interrupt,
    clear,
    reconnect,
    settings,
    reveal_password,
    edit_password,
    control,  // Ctrl+key; the character says which
    alt,      // Alt+key; the character says which
    function, // F1, F7..F12 for programs; the character is the number
    copy,     // Ctrl+Shift+C, Ctrl+Insert
    paste     // Ctrl+Shift+V, Shift+Insert
};
using KeyHandler = void (*)(Key, char);
char hid_character(unsigned key, unsigned modifiers, bool caps) noexcept;
// One key press (USB HID usage and modifier byte) to app keys; caps is Caps Lock state.
void translate_key(unsigned usage, unsigned modifiers, bool &caps, KeyHandler callback) noexcept;
class Input
{
  public:
    void tick(std::uint64_t now, KeyHandler callback) noexcept;
    std::uint32_t pressed() const noexcept
    {
        return pressed_;
    }

    std::uint32_t held() const noexcept
    {
        return buttons_;
    }
    // USB keyboard modifiers of the latest sample (0x22 Shift, 0x11 Ctrl).
    std::uint32_t modifiers() const noexcept
    {
        return modifiers_;
    }
    // Research hook: every raw keyboard sample, before key handling.
    using RawKeyboard = void (*)(std::uint32_t modifiers, const std::uint16_t *keys, int count,
                                 bool connected, bool intercepted);
    RawKeyboard raw_keyboard = nullptr;

  private:
    int user_ = -1, pad_ = -1, keyboard_ = -1;
    bool initialized_ = false, caps_ = false;
    std::uint64_t scan_ = 0, repeat_at_ = 0;
    std::uint16_t repeat_key_ = 0;
    std::array<std::uint16_t, 16> previous_{};
    std::uint32_t buttons_ = 0, pressed_ = 0, modifiers_ = 0;
    std::uint32_t repeat_buttons_ = 0;
    std::uint64_t pad_repeat_at_ = 0;
    void key(unsigned usage, unsigned modifiers, KeyHandler callback) noexcept;
};
} // namespace term
