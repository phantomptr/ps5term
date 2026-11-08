// PS5Term - USB keyboard to app keys: what reaches programs and what the app keeps.
// GPL-3.0-or-later.
#include "input.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using term::Key;
namespace
{
struct Event
{
    Key key;
    char ch;
};
std::vector<Event> events;
void record(Key k, char ch)
{
    events.push_back({k, ch});
}
Event press(unsigned usage, unsigned mods = 0)
{
    events.clear();
    bool caps = false;
    term::translate_key(usage, mods, caps, record);
    assert(events.size() == 1);
    return events[0];
}
constexpr unsigned shift = 0x02, ctrl = 0x01, alt = 0x04;
} // namespace
int main()
{
    // Shift+F10 opens the actions menu (keyboards without a Menu key); F10 alone is
    // the program's (htop quits with it).
    assert(press(67, shift).key == Key::menu);
    assert(press(101).key == Key::menu);
    auto f10 = press(67);
    assert(f10.key == Key::function && f10.ch == 10);
    // F-keys keep their modifiers for programs: number | modifiers << 4.
    auto ctrl_f7 = press(64, ctrl);
    assert(ctrl_f7.key == Key::function && (ctrl_f7.ch & 0x0f) == 7 &&
           ((ctrl_f7.ch >> 4) & 7) == 4);
    // F2..F6 are app keys marked as function keys, so a running program can have them.
    for (unsigned usage : {59U, 60U, 61U, 62U, 63U})
        assert(press(usage).ch & term::from_function_key);
    assert(press(63).key == Key::settings && press(61).key == Key::toggle_keyboard);
    // App shortcuts that work everywhere, even when F-keys belong to a program.
    assert(press(22, ctrl | shift).key == Key::settings && press(22, ctrl | shift).ch == 0);
    assert(press(14, ctrl | shift).key == Key::toggle_keyboard);
    assert(press(44, ctrl | shift).key == Key::select_text);
    auto tab3 = press(32, ctrl | shift);
    assert(tab3.key == Key::go_tab && tab3.ch == 2);
    // Modifiers on editing keys: Alt+Backspace (readline word delete), Shift+Tab, Delete, Insert.
    auto alt_bs = press(42, alt);
    assert(alt_bs.key == Key::backspace && (alt_bs.ch & 2));
    assert(press(43, shift).key == Key::backtab);
    assert(press(76).key == Key::del && press(73).key == Key::insert);
    // Ctrl/Shift+Insert stay copy/paste; Ctrl+C stays an interrupt character.
    assert(press(73, ctrl).key == Key::copy && press(73, shift).key == Key::paste);
    auto ctrl_c = press(6, ctrl);
    assert(ctrl_c.key == Key::control && ctrl_c.ch == 'c');
    assert(press(6, ctrl | shift).key == Key::copy);
    std::puts(
        "PASS: keyboard mapping: Shift+F10 menu, F-keys with modifiers, Ctrl+Shift shortcuts, "
        "editing keys");
    return 0;
}
