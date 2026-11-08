// PS5Term - mouse pointer math, hit-testing and selection. GPL-3.0-or-later.
#include "pointer.hpp"
#include "terminal.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
using namespace term;
using namespace term::ui;
int main()
{
    // Pointer: speed scaling with sub-pixel remainder, clamped to the screen.
    Pointer p;
    assert(p.x == 960 && p.y == 540);
    p.move(10, -10, 100);
    assert(p.x == 970 && p.y == 530);
    p.move(1, 1, 50);
    assert(p.x == 970 && p.y == 530);
    p.move(1, 1, 50); // two half-pixel moves make one pixel
    assert(p.x == 971 && p.y == 531);
    p.move(3, 0, 300);
    assert(p.x == 980);
    p.move(-100000, -100000, 300);
    assert(p.x == 0 && p.y == 0);
    p.move(100000, 100000, 300);
    assert(p.x == screen_width - 1 && p.y == screen_height - 1);
    assert(nearest_speed(100) == default_speed && speeds[nearest_speed(160)] == 150 &&
           speeds[nearest_speed(9999)] == 300 && speeds[nearest_speed(0)] == 50);

    // Visibility: shown on mouse use, fades after 3 s, hidden by other input
    // only once the mouse has been still for a second.
    Visibility v;
    assert(v.alpha(0) == 0);
    v.mouse(1000000);
    assert(v.alpha(1000000) == 255 && v.alpha(4000000) == 255);
    assert(v.alpha(4250000) > 0 && v.alpha(4250000) < 255 && v.alpha(4600000) == 0);
    v.other_input(1500000);
    assert(v.shown);
    v.other_input(2000000);
    assert(!v.shown && v.alpha(2000001) == 0);

    // Clicks: double and triple within 450 ms and 6 px, then start over.
    ClickCounter clicks;
    assert(clicks.press(1000000, 100, 100) == 1);
    assert(clicks.press(1300000, 103, 98) == 2);
    assert(clicks.press(1600000, 103, 98) == 3);
    assert(clicks.press(1700000, 103, 98) == 1);
    assert(clicks.press(2300000, 103, 98) == 1); // too slow
    assert(clicks.press(2400000, 140, 98) == 1); // too far

    // Output grid uses the same metrics as the native text renderer.
    Grid g;
    static_assert(text_metrics::cell_width == 10 && text_metrics::row_height == 20);
    static_assert(Screen::columns == text_metrics::columns);
    assert(g.columns == Screen::columns && g.area().x + g.area().w <= 1856);
    auto cell = cell_at(g, 96, 226);
    assert(cell.inside && cell.row == 0 && cell.col == 0);
    cell = cell_at(g, 96 + g.cell_width * 5 + g.cell_width - 1,
                   222 + g.row_height * 3 + g.row_height - 1);
    assert(cell.inside && cell.row == 3 && cell.col == 5);
    cell = cell_at(g, 10, 2000);
    assert(!cell.inside && cell.row == g.rows - 1 && cell.col == 0);
    cell = cell_at(g, 1900, 100);
    assert(!cell.inside && cell.row == 0 && cell.col == g.columns - 1);

    // Input line: nearest character boundary, clamped to the text.
    assert(input_cursor(128, 0, 5) == 0 &&
           input_cursor(128 + text_metrics::cell_width * 2 + text_metrics::cell_width / 2 + 1, 0,
                        5) == 3);
    assert(input_cursor(1700, 0, 5) == 5 && input_cursor(20, 10, 200) == 10);

    // On-screen keyboard keys and the gaps between them.
    assert(key_at(84, keyboard_top) == 0 && key_at(84 + 147 + 10, keyboard_top + 35 + 5) == 13);
    assert(key_at(84 + 140, keyboard_top + 12) == -1 &&
           key_at(84 + 11 * 147 + 134, keyboard_top + 3 * 35 + 29) == 47);
    assert(key_at(84, keyboard_top + 4 * 35) == 48 &&
           key_at(84 + 11 * 147, keyboard_top + 4 * 35) == 59);
    // The F1-F10, Del, Ins row.
    assert(key_at(84, keyboard_top + 5 * 35) == 60 &&
           key_at(84 + 11 * 147, keyboard_top + 5 * 35) == 71);
    // The keyboard stays clear of the terminal rows above and the input box below.
    assert(keyboard_top > 222 + 19 * 20 && key_rect(71).y + key_rect(71).h < 850);

    // Menu: kept on screen, items 44 px tall.
    auto menu = menu_rect(1900, 1070);
    assert(menu.x + menu.w <= screen_width && menu.y + menu.h <= screen_height);
    menu = menu_rect(500, 400);
    assert(menu_item(menu, 510, 410) == MenuItem::copy);
    assert(menu_item(menu, 510, 404 + 44 + 1) == MenuItem::paste);
    assert(menu_item(menu, 510, 404 + 44 * 3 + 2) == MenuItem::clear);
    assert(menu_item(menu, 499, 410) == MenuItem::none &&
           menu_item(menu, 510, 401) == MenuItem::none);

    // Wheel: 3 lines or a 12-line page per notch, within [0, max].
    assert(wheel_scroll(0, 1, false, 100) == 3 && wheel_scroll(0, 2, true, 100) == 24);
    assert(wheel_scroll(10, -1, false, 100) == 7 && wheel_scroll(2, -1, false, 100) == 0);
    assert(wheel_scroll(99, 1, false, 100) == 100 && wheel_scroll(0, 1, false, 0) == 0);
    assert(wheel_scroll(500, 0, false, 100) == 100);

    // Words.
    std::string_view line = "ls -la /data/ps5term; echo  ok";
    auto w = word_at(line, 9);
    assert(line.substr(w.first, w.last - w.first + 1) == "/data/ps5term");
    w = word_at(line, 20);
    assert(w.first == 20 && w.last == 20); // ';' alone
    w = word_at(line, 26);
    assert(w.first == 26 && w.last == 27); // the run of spaces
    w = word_at(line, 200);
    assert(w.first == 200 && w.last == 200);

    // Selection text across lines, trailing spaces dropped, ids stable.
    Screen screen;
    screen.feed("first line   \nsecond\nthird line");
    Selection sel;
    char out[256];
    assert(selection_text(screen, sel, out, sizeof(out)) == 0);
    sel.begin({0, 6}, {0, 6});
    sel.extend({2, 2}, {2, 2});
    auto n = selection_text(screen, sel, out, sizeof(out));
    assert(std::string(out, n) == "line\nsecond\nthi");
    assert(sel.contains(1, 50) && !sel.contains(0, 5) && !sel.contains(2, 3));
    sel.begin({2, 2}, {2, 2}); // dragging backwards is the same range
    sel.extend({0, 6}, {0, 6});
    assert(selection_text(screen, sel, out, sizeof(out)) == n);
    sel.begin({0, 0}, {0, 107}); // a whole line (triple click)
    assert(std::string(out, selection_text(screen, sel, out, sizeof(out))) == "first line");
    assert(selection_text(screen, sel, out, 3) == 3);
    for (int i = 0; i < 600; ++i)
        screen.feed("row\n");
    assert(screen.dropped() > 0 && screen.dropped() + screen.count() == 603);
    assert(selection_text(screen, sel, out, sizeof(out)) == 0); // scrolled out
    sel.begin({screen.dropped(), 0}, {screen.dropped(), 107});
    assert(std::string(out, selection_text(screen, sel, out, sizeof(out))) == "row");
    auto before = screen.dropped() + screen.count();
    screen.clear();
    assert(screen.dropped() == before && screen.count() == 1);

    // Paste: one editor line.
    n = paste_text("echo a\n\techo b\x01\r\n", out, sizeof(out));
    assert(std::string(out, n) == "echo a  echo b");
    assert(paste_text("abcdef", out, 3) == 3);
    Clipboard clip;
    clip.set("hello");
    assert(clip.text() == "hello");

    // Buttons.
    assert(settings_button_at(1600, 250) == SettingsButton::toggle_ssh);
    assert(settings_button_at(90, 945) == SettingsButton::toggle_ssh);
    assert(settings_button_at(90, 985) == SettingsButton::show_password);
    assert(settings_button_at(700, 985) == SettingsButton::retry);
    assert(settings_button_at(90, 1025) == SettingsButton::back);
    assert(settings_button_at(1450, 880) == SettingsButton::speed_down);
    assert(settings_button_at(1700, 880) == SettingsButton::speed_up);
    assert(settings_button_at(900, 500) == SettingsButton::none);
    assert(main_button_at(1500, 168, false) == MainButton::settings);
    assert(main_button_at(1190, 612, false) == MainButton::none);
    assert(main_button_at(1190, 612, true) == MainButton::shift);
    assert(main_button_at(1300, 612, true) == MainButton::del);
    assert(main_button_at(1400, 612, true) == MainButton::run);

    // Speed preference round trip.
    char dir[] = "/tmp/ps5term-pointer-XXXXXX";
    assert(mkdtemp(dir));
    std::string path = std::string(dir) + "/mouse.speed";
    assert(load_speed(path.c_str()) == default_speed);
    assert(save_speed(dir, path.c_str(), 5) && load_speed(path.c_str()) == 5);
    assert(!save_speed(dir, path.c_str(), 99));
    FILE *bad = std::fopen(path.c_str(), "w");
    std::fputs("fast\n", bad);
    std::fclose(bad);
    assert(load_speed(path.c_str()) == default_speed);
    unlink(path.c_str());
    rmdir(dir);
    std::puts("PASS: pointer motion, visibility, clicks, hit-testing, wheel, words, selection, "
              "paste, buttons, speed preference");
}
