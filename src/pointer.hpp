// PS5Term - mouse pointer math, hit-testing and text selection. GPL-3.0-or-later.
//
// Pure functions and small value types, host-tested in tests/terminal. The
// platform mouse (libSceMouse) lives in mouse.cpp; App wires the two together.
// Layout constants mirror what App::draw paints, so a point on screen maps to
// the same cell, key or button the user sees.
#pragma once
#include "i18n.hpp"
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string_view>
#include "text_metrics.hpp"
namespace term
{
class Screen;
class TerminalModel;
namespace ui
{
constexpr int screen_width = 1920, screen_height = 1080;
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    constexpr bool contains(int px, int py) const noexcept
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

// Pointer speed steps (percent); the setting stores one of these values.
constexpr std::array<unsigned, 7> speeds{50, 75, 100, 125, 150, 200, 300};
constexpr std::size_t default_speed = 2; // 1.0x
std::size_t nearest_speed(unsigned percent) noexcept;

// The pointer, moved by relative mouse deltas and clamped to the screen.
// Sub-pixel motion is kept, so slow speeds still move on small deltas.
struct Pointer
{
    int x = screen_width / 2, y = screen_height / 2;
    int rest_x = 0, rest_y = 0; // remainder, in 1/100 pixel
    void move(int dx, int dy, unsigned percent) noexcept;
};

// Shown on mouse activity, fades after 3 s idle, hidden at once when other
// input arrives while the mouse has been still for a second.
struct Visibility
{
    static constexpr std::uint64_t idle = 3000000, fade = 500000, other = 1000000;
    std::uint64_t last = 0;
    bool shown = false;
    void mouse(std::uint64_t now) noexcept
    {
        shown = true;
        last = now;
    }
    void other_input(std::uint64_t now) noexcept
    {
        if (shown && now - last >= other)
            shown = false;
    }
    // 0 (hidden) .. 255 (opaque).
    unsigned alpha(std::uint64_t now) const noexcept;
};

// Double and triple clicks: presses within 450 ms and 6 px count up 1,2,3.
struct ClickCounter
{
    std::uint64_t last = 0;
    int x = 0, y = 0;
    unsigned count = 0;
    unsigned press(std::uint64_t now, int px, int py) noexcept;
};

// Shared native-pixel geometry keeps text selection aligned with the renderer.
struct Grid
{
    int left = 96, top = 222, cell_width = text_metrics::cell_width,
        row_height = text_metrics::row_height;
    std::size_t rows = text_metrics::rows, columns = text_metrics::columns;
    constexpr Rect area() const noexcept
    {
        return {left, top, static_cast<int>(columns) * cell_width,
                static_cast<int>(rows) * row_height};
    }
};
// Split view: the left pane's text width in pixels; the right pane gets the rest.
// The panes share 1640 px with a 68 px gutter holding the divider.
constexpr int split_total = 1640, split_min = 400, split_default = 820;
constexpr int split_clamp(int left) noexcept
{
    return left < split_min                 ? split_min
           : left > split_total - split_min ? split_total - split_min
                                            : left;
}
constexpr int divider_x(int left) noexcept
{
    return 96 + left + 40;
}
struct Cell
{
    std::size_t row = 0, col = 0;
    bool inside = false; // false: the point was outside and was clamped
};
Cell cell_at(const Grid &grid, int px, int py) noexcept;

constexpr Rect search_previous{1040, 862, 170, 44}, search_next{1220, 862, 150, 44},
    search_refresh{1380, 862, 180, 44}, search_close{1580, 862, 240, 44};

// Input line: text starts at x = 128, using the terminal font advance, showing the
// editor text from `offset`. Returns the editor position nearest to px.
constexpr Rect input_area{64, 850, 1792, 76};
std::size_t input_cursor(int px, std::size_t offset, std::size_t size) noexcept;

// On-screen keyboard: 48 character keys then one row of terminal keys, 12 per row.
// 48 characters, a row of terminal keys (Esc..End), and a row of F1-F10, Del, Ins.
constexpr std::size_t character_keys = 48, keyboard_keys = 72;
constexpr int keyboard_top = 636;
constexpr Rect key_rect(std::size_t index) noexcept
{
    return {84 + static_cast<int>(index % 12) * 147,
            keyboard_top + static_cast<int>(index / 12) * 35, 135, 30};
}
int key_at(int px, int py) noexcept; // -1: none

// Clickable labels: the hit area pads the text a little. scale is the font
// style, using the same advance and height as the renderer.
constexpr Rect label_rect(int x, int y, std::size_t chars, unsigned scale) noexcept
{
    return {x - 8, y - 8, static_cast<int>(chars * text_metrics::advance(scale)) + 16,
            static_cast<int>(text_metrics::height(scale)) + 16};
}

// Right-click menu.
enum class MenuItem
{
    none = -1,
    copy,
    paste,
    select_all,
    clear,
    select_text,
    search,
    new_tab,
    close_tab,
    previous_tab,
    next_tab,
    toggle_split,
    focus_pane,
    divider_left,
    divider_right
};
// No pointer tables in constant data: the native converter cannot place a
// .data.rel.ro section (see tools/build.sh), so labels come from a switch.
constexpr std::size_t menu_items = 14;
constexpr std::string_view menu_label(MenuItem item) noexcept
{
    switch (item)
    {
    case MenuItem::copy:
        return N_("Copy");
    case MenuItem::paste:
        return N_("Paste");
    case MenuItem::select_all:
        return N_("Select all");
    case MenuItem::clear:
        return N_("Clear");
    case MenuItem::toggle_split:
        return N_("Split / unsplit");
    case MenuItem::focus_pane:
        return N_("Focus other pane");
    case MenuItem::divider_left:
        return N_("Divider left");
    case MenuItem::divider_right:
        return N_("Divider right");
    case MenuItem::new_tab:
        return N_("New tab");
    case MenuItem::close_tab:
        return N_("Close tab");
    case MenuItem::previous_tab:
        return N_("Previous tab");
    case MenuItem::next_tab:
        return N_("Next tab");
    case MenuItem::search:
        return N_("Find text");
    case MenuItem::select_text:
        return N_("Select text");
    case MenuItem::none:
        break;
    }
    return {};
}
constexpr int menu_width = 260, menu_item_height = 44;
Rect menu_rect(int px, int py) noexcept; // opens at the pointer, kept on screen
MenuItem menu_item(const Rect &menu, int px, int py) noexcept;

// Wheel: 3 lines per notch, a page (12 lines) per notch with Shift. Positive
// is away from the user (up): it scrolls back into history.
constexpr std::size_t wheel_lines = 3, wheel_page = 12;
std::size_t wheel_scroll(std::size_t scroll, int wheel, bool page, std::size_t max) noexcept;

// A text position: line is Screen::dropped() + index, so it survives output.
struct TextPos
{
    std::uint64_t line = 0;
    std::size_t col = 0;
    constexpr bool operator==(const TextPos &) const noexcept = default;
    constexpr bool operator<(const TextPos &o) const noexcept
    {
        return line < o.line || (line == o.line && col < o.col);
    }
};
// Word bounds around col (inclusive): a run of word characters, of spaces, or
// one punctuation character. Past the end of the text: {col, col}.
struct Span
{
    std::size_t first = 0, last = 0;
};
Span word_at(std::string_view text, std::size_t col) noexcept;

// A selection is the union of the unit first selected (a character, word or
// line) and the unit under the pointer now, so drags extend by that unit.
class Selection
{
  public:
    void begin(TextPos first, TextPos last) noexcept
    {
        anchor_first_ = head_first_ = first;
        anchor_last_ = head_last_ = last;
        active_ = true;
    }
    void extend(TextPos first, TextPos last) noexcept
    {
        head_first_ = first;
        head_last_ = last;
    }
    void clear() noexcept
    {
        active_ = false;
    }
    bool active() const noexcept
    {
        return active_;
    }
    TextPos first() const noexcept
    {
        return head_first_ < anchor_first_ ? head_first_ : anchor_first_;
    }
    TextPos last() const noexcept
    {
        return anchor_last_ < head_last_ ? head_last_ : anchor_last_;
    }
    bool contains(std::uint64_t line, std::size_t col) const noexcept
    {
        TextPos p{line, col};
        return active_ && !(p < first()) && !(last() < p);
    }

  private:
    TextPos anchor_first_{}, anchor_last_{}, head_first_{}, head_last_{};
    bool active_ = false;
};
// Selected text, lines joined with '\n' and trailing spaces dropped. Returns
// the length written (truncated to capacity, no terminator).
std::size_t selection_text(const Screen &screen, const Selection &selection, char *out,
                           std::size_t capacity) noexcept;
std::size_t selection_text(const TerminalModel &model, const Selection &selection, char *out,
                           std::size_t capacity) noexcept;
// Text for the one-line editor: newlines and tabs become spaces, other
// control characters are dropped, trailing whitespace is trimmed.
std::size_t paste_text(std::string_view text, char *out, std::size_t capacity) noexcept;

// PS5Term's own clipboard (the system has none available to the app).
class Clipboard
{
  public:
    static constexpr std::size_t capacity = 16384;
    void set(std::string_view text) noexcept;
    std::string_view text() const noexcept
    {
        return {data_.data(), size_};
    }
    char *buffer() noexcept
    {
        return data_.data();
    }
    void resize(std::size_t size) noexcept
    {
        size_ = size < capacity ? size : capacity;
    }

  private:
    std::array<char, capacity> data_{};
    std::size_t size_ = 0;
};

// Settings screen buttons.
enum class SettingsButton
{
    none,
    toggle_ssh,
    restart_ssh,
    show_password,
    retry,
    back,
    speed_down,
    speed_up,
    toggle_linux,
    toggle_host_mount,
    expand_storage,
    text_size,
    open_alpine,
    about,
    language
};
// Settings rows: SSH 208-628, Linux 644-848 (buttons on their own row, status lines
// full width below), mouse/text 864-920, actions 932-1048.
constexpr Rect text_size_button{1260, 870, 160, 44};
constexpr Rect language_button{640, 870, 600, 44};
constexpr Rect alpine_button{668, 712, 300, 40};
constexpr Rect storage_expand{988, 712, 816, 40};
constexpr Rect linux_switch{1320, 656, 484, 44};
constexpr Rect host_mount_switch{88, 712, 560, 40};
constexpr Rect ssh_retry{1320, 234, 224, 52};
constexpr Rect ssh_switch{1580, 234, 224, 52};
constexpr Rect speed_down_rect{1440, 870, 72, 44}, speed_up_rect{1680, 870, 72, 44};
constexpr Rect help_toggle = label_rect(84, 940, 26, 2), help_password = label_rect(84, 980, 33, 2),
               help_retry = label_rect(676, 980, 32, 2), help_back = label_rect(84, 1020, 32, 2),
               help_about = label_rect(676, 1020, 22, 2);
SettingsButton settings_button_at(int px, int py) noexcept;

// Main screen buttons.
enum class MainButton
{
    none,
    settings,
    keyboard,
    reconnect,
    interrupt,
    shift,
    del,
    run
};
constexpr Rect tab_button(unsigned index) noexcept
{
    return {64 + static_cast<int>(index) * 150, 148, 142, 44};
}
constexpr Rect split_button{340, 52, 330, 40}, focus_pane_button{340, 96, 330, 36};
constexpr Rect new_tab_button{700, 60, 210, 44}, close_tab_button{930, 60, 210, 44};
constexpr Rect keyboard_button{700, 148, 210, 44}, reconnect_button{930, 148, 210, 44},
    interrupt_button{1160, 148, 210, 44};
constexpr Rect settings_label = label_rect(1480, 166, 18, 1);
constexpr Rect osk_shift = label_rect(1180, 610, 8, 1), osk_delete = label_rect(1268, 610, 13, 1),
               osk_run = label_rect(1396, 610, 6, 1);
MainButton main_button_at(int px, int py, bool keyboard) noexcept;

// Mouse speed preference: the percent as text, e.g. "150\n".
std::size_t load_speed(const char *path) noexcept; // index into speeds
bool save_speed(const char *directory, const char *path, std::size_t index) noexcept;

// Opt-in diagnostics: when the flag file exists at start, mouse events and
// actions are appended to the log (bounded). Used for console verification.
class EventLog
{
  public:
    void open(const char *flag, const char *path) noexcept;
    // snprintf-formatted (the app imports snprintf, not vsnprintf).
    template <typename First, typename... Rest>
    void line(const char *format, First first, Rest... rest) noexcept
    {
        if (fd_ < 0 || lines_ >= 4000)
            return;
        char text[224];
        int size = std::snprintf(text, sizeof(text), format, first, rest...);
        if (size > 0)
            write_line(text, static_cast<std::size_t>(size) < sizeof(text)
                                 ? static_cast<std::size_t>(size)
                                 : sizeof(text) - 1);
    }
    void line(const char *text) noexcept
    {
        if (fd_ >= 0 && lines_ < 4000)
            write_line(text, std::strlen(text));
    }
    bool enabled() const noexcept
    {
        return fd_ >= 0;
    }

  private:
    int fd_ = -1;
    unsigned lines_ = 0;
    void write_line(const char *text, std::size_t size) noexcept;
};
} // namespace ui
} // namespace term
