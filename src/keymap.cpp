// PS5Term - USB HID US keyboard mapping. GPL-3.0-or-later.
#include "input.hpp"
namespace term
{
char hid_character(unsigned k, unsigned mods, bool caps) noexcept
{
    bool shift = (mods & 0x22) != 0;
    if (k >= 4 && k <= 29)
        return static_cast<char>((shift != caps ? 'A' : 'a') + k - 4);
    if (k >= 30 && k <= 39)
        return (shift ? "!@#$%^&*()" : "1234567890")[k - 30];
    if (k == 44)
        return ' ';
    if (k >= 45 && k <= 56)
        return (shift ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[k - 45];
    return 0;
}
// One USB HID key press to app keys. Host-testable: no PS5 calls here.
void translate_key(unsigned k, unsigned mods, bool &caps_, KeyHandler cb) noexcept
{
    if (k == 57)
    {
        caps_ = !caps_;
        return;
    }
    if ((mods & 0x11) && (mods & 0x22) && (k == 8 || k == 18))
    {
        cb(k == 8 ? Key::toggle_split : Key::focus_pane, 0);
        return;
    }
    if ((mods & 0x11) && (mods & 0x22) && (k == 79 || k == 80))
    {
        cb(k == 80 ? Key::divider_left : Key::divider_right, 0);
        return;
    }
    if ((mods & 0x11) && (mods & 0x22) && (k == 23 || k == 26))
    {
        cb(k == 23 ? Key::new_tab : Key::close_tab, 0);
        return;
    }
    // App actions that work everywhere, even where F-keys belong to a program:
    // Ctrl+Shift+S Settings, K on-screen keyboard, Space select text, 1-4 a tab.
    if ((mods & 0x11) && (mods & 0x22) && (k == 22 || k == 14 || k == 44))
    {
        cb(k == 22 ? Key::settings : k == 14 ? Key::toggle_keyboard : Key::select_text, 0);
        return;
    }
    if ((mods & 0x11) && (mods & 0x22) && k >= 30 && k <= 33)
    {
        cb(Key::go_tab, static_cast<char>(k - 30));
        return;
    }
    if ((mods & 0x11) && (k == 75 || k == 78))
    {
        cb(k == 75 ? Key::previous_tab : Key::next_tab, 0);
        return;
    }
    // Terminal clipboard: Ctrl+Shift+C/V (plain Ctrl+C stays interrupt),
    // Ctrl+Insert copies and Shift+Insert pastes.
    if ((mods & 0x11) && (mods & 0x22) && k == 9)
    {
        cb(Key::search, 0);
        return;
    }
    if ((mods & 0x11) && (mods & 0x22) && (k == 6 || k == 25))
    {
        cb(k == 6 ? Key::copy : Key::paste, 0);
        return;
    }
    if (k == 73 && (mods & (0x11 | 0x22)))
    {
        cb(mods & 0x11 ? Key::copy : Key::paste, 0);
        return;
    }
    if (mods & 0x11)
    {
        // Every Ctrl combination reaches the app; it knows which mode it is in.
        const char base = hid_character(k, mods & 0x22, false);
        if (base)
        {
            cb(Key::control, base);
            return;
        }
    }
    if (mods & 0x44)
    {
        const char character = hid_character(k, mods & 0x22, caps_);
        if (character)
        {
            cb(Key::alt, character);
            return;
        }
    }
    // The numeric keypad, as typed characters (no NumLock state is tracked).
    if ((k >= 84 && k <= 87) || (k >= 89 && k <= 99))
    {
        static constexpr char keypad[] = "/*-+?1234567890.";
        cb(Key::character, keypad[k - 84]);
        return;
    }
    // Arrows and page keys carry modifiers for programs: shift 1, alt 2, control 4.
    const char navigation = static_cast<char>(((mods & 0x22) ? 1 : 0) | ((mods & 0x44) ? 2 : 0) |
                                              ((mods & 0x11) ? 4 : 0));
    // Shift+F10 and the Menu key open the actions menu (checked before F10 itself).
    if (k == 101 || (k == 67 && (mods & 0x22) && !(mods & 0x55)))
    {
        cb(Key::menu, 0);
        return;
    }
    // F1 and F7..F12 belong to programs, with their modifiers (number | modifiers << 4).
    if (k == 58 || (k >= 64 && k <= 69))
    {
        cb(Key::function, static_cast<char>((k == 58 ? 1 : k - 57) | (navigation << 4)));
        return;
    }
    switch (k)
    {
    case 43:
        cb(mods & 0x22 ? Key::backtab : Key::tab, static_cast<char>(navigation & ~1));
        return;
    case 61:
        cb(Key::toggle_keyboard, static_cast<char>(from_function_key | navigation));
        return;
    case 40:
    case 88:
        cb(Key::enter, navigation);
        return;
    case 41:
        cb(Key::escape, 0);
        return;
    case 42:
        cb(Key::backspace, navigation);
        return;
    case 76:
        cb(Key::del, navigation);
        return;
    case 73: // Ctrl/Shift+Insert are copy/paste (above); plain Insert is the program's.
        cb(Key::insert, navigation);
        return;
    case 74:
        cb(Key::home, navigation);
        return;
    case 77:
        cb(Key::end, navigation);
        return;
    case 79:
        cb(Key::right, navigation);
        return;
    case 80:
        cb(Key::left, navigation);
        return;
    case 81:
        cb(Key::down, navigation);
        return;
    case 82:
        cb(Key::up, navigation);
        return;
    case 75:
        cb(Key::pageup, navigation);
        return;
    case 78:
        cb(Key::pagedown, navigation);
        return;
    // F2..F6 are app keys outside programs; from_function_key lets a running Linux
    // program have them instead (see App::key).
    case 60:
        cb(Key::edit_password, static_cast<char>(from_function_key | navigation));
        return;
    case 59:
        cb(Key::reveal_password, static_cast<char>(from_function_key | navigation));
        return;
    case 63:
        cb(Key::settings, static_cast<char>(from_function_key | navigation));
        return;
    case 62:
        cb(Key::reconnect, static_cast<char>(from_function_key | navigation));
        return;
    default:
        break;
    }
    if (char ch = hid_character(k, mods, caps_))
        cb(Key::character, ch);
}
} // namespace term
