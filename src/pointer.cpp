// PS5Term - mouse pointer math, hit-testing and text selection. GPL-3.0-or-later.
#include "pointer.hpp"
#include "terminal.hpp"
#include "terminal_model.hpp"
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace term::ui
{
namespace
{
int clamp(int value, int low, int high) noexcept
{
    return value < low ? low : value > high ? high : value;
}
bool word_char(char ch) noexcept
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '.' || ch == '/' || ch == '~' || ch == ':' ||
           ch == '@' || ch == '%' || ch == '+' || ch == '=';
}
int kind(char ch) noexcept
{
    return ch == ' ' ? 0 : word_char(ch) ? 1 : 2;
}
} // namespace

std::size_t nearest_speed(unsigned percent) noexcept
{
    std::size_t best = default_speed;
    unsigned distance = ~0U;
    for (std::size_t i = 0; i < speeds.size(); ++i)
    {
        unsigned d = speeds[i] > percent ? speeds[i] - percent : percent - speeds[i];
        if (d < distance)
        {
            distance = d;
            best = i;
        }
    }
    return best;
}

void Pointer::move(int dx, int dy, unsigned percent) noexcept
{
    // Device deltas are small; clamp absurd ones so the math cannot overflow.
    dx = clamp(dx, -4096, 4096);
    dy = clamp(dy, -4096, 4096);
    int sx = dx * static_cast<int>(percent) + rest_x, sy = dy * static_cast<int>(percent) + rest_y;
    rest_x = sx % 100;
    rest_y = sy % 100;
    x = clamp(x + sx / 100, 0, screen_width - 1);
    y = clamp(y + sy / 100, 0, screen_height - 1);
}

unsigned Visibility::alpha(std::uint64_t now) const noexcept
{
    if (!shown)
        return 0;
    std::uint64_t quiet = now > last ? now - last : 0;
    if (quiet <= idle)
        return 255;
    if (quiet >= idle + fade)
        return 0;
    return static_cast<unsigned>(255 - (quiet - idle) * 255 / fade);
}

unsigned ClickCounter::press(std::uint64_t now, int px, int py) noexcept
{
    int dx = px - x, dy = py - y;
    bool near = dx >= -6 && dx <= 6 && dy >= -6 && dy <= 6;
    count = count && count < 3 && near && now - last <= 450000 ? count + 1 : 1;
    last = now;
    x = px;
    y = py;
    return count;
}

Cell cell_at(const Grid &grid, int px, int py) noexcept
{
    Cell cell;
    cell.inside = grid.area().contains(px, py);
    int col = (px - grid.left) / grid.cell_width, row = (py - grid.top) / grid.row_height;
    if (px < grid.left)
        col = 0;
    if (py < grid.top)
        row = 0;
    cell.col = static_cast<std::size_t>(clamp(col, 0, static_cast<int>(grid.columns) - 1));
    cell.row = static_cast<std::size_t>(clamp(row, 0, static_cast<int>(grid.rows) - 1));
    return cell;
}

std::size_t input_cursor(int px, std::size_t offset, std::size_t size) noexcept
{
    constexpr int width = text_metrics::cell_width;
    int col = px < 128 ? 0 : (px - 128 + width / 2) / width;
    std::size_t position = offset + static_cast<std::size_t>(col);
    return position < size ? position : size;
}

int key_at(int px, int py) noexcept
{
    for (std::size_t i = 0; i < keyboard_keys; ++i)
        if (key_rect(i).contains(px, py))
            return static_cast<int>(i);
    return -1;
}

Rect menu_rect(int px, int py) noexcept
{
    int height = static_cast<int>(menu_items) * menu_item_height + 8;
    return {clamp(px, 0, screen_width - menu_width), clamp(py, 0, screen_height - height),
            menu_width, height};
}

MenuItem menu_item(const Rect &menu, int px, int py) noexcept
{
    if (!menu.contains(px, py) || py < menu.y + 4)
        return MenuItem::none;
    int index = (py - menu.y - 4) / menu_item_height;
    if (index >= static_cast<int>(menu_items))
        return MenuItem::none;
    return static_cast<MenuItem>(index);
}

std::size_t wheel_scroll(std::size_t scroll, int wheel, bool page, std::size_t max) noexcept
{
    std::size_t step = page ? wheel_page : wheel_lines;
    std::size_t notches = static_cast<std::size_t>(wheel < 0 ? -static_cast<long>(wheel) : wheel);
    std::size_t amount = notches > 1000 ? 1000 * step : notches * step;
    if (wheel > 0)
        scroll = max - (scroll < max ? scroll : max) > amount ? scroll + amount : max;
    else if (wheel < 0)
        scroll = scroll > amount ? scroll - amount : 0;
    return scroll < max ? scroll : max;
}

Span word_at(std::string_view text, std::size_t col) noexcept
{
    if (col >= text.size())
        return {col, col};
    int k = kind(text[col]);
    if (k == 2)
        return {col, col};
    std::size_t first = col, last = col;
    while (first > 0 && kind(text[first - 1]) == k)
        --first;
    while (last + 1 < text.size() && kind(text[last + 1]) == k)
        ++last;
    return {first, last};
}

std::size_t selection_text(const Screen &screen, const Selection &selection, char *out,
                           std::size_t capacity) noexcept
{
    if (!selection.active() || !out || capacity == 0)
        return 0;
    auto first = selection.first(), last = selection.last();
    std::size_t size = 0;
    bool any = false;
    for (std::uint64_t serial = first.line; serial <= last.line; ++serial)
    {
        if (serial < screen.dropped() || serial - screen.dropped() >= screen.count())
            continue;
        auto text = screen.line(static_cast<std::size_t>(serial - screen.dropped()));
        std::size_t from = serial == first.line ? first.col : 0;
        std::size_t to = serial == last.line ? last.col + 1 : text.size();
        if (to > text.size())
            to = text.size();
        while (to > from && text[to - 1] == ' ')
            --to;
        if (any && size < capacity)
            out[size++] = '\n';
        any = true;
        for (std::size_t i = from; i < to && size < capacity; ++i)
            out[size++] = text[i];
    }
    return size;
}

std::size_t selection_text(const TerminalModel &model, const Selection &selection, char *out,
                           std::size_t capacity) noexcept
{
    if (!selection.active() || !out || capacity == 0)
        return 0;
    auto first = selection.first(), last = selection.last();
    const auto history_end = model.history_end(), history_first = model.history_first();
    const auto cols = static_cast<std::size_t>(model.columns());
    std::size_t size = 0;
    bool any = false, soft_wrapped = false;
    for (std::uint64_t serial = first.line; serial <= last.line; ++serial)
    {
        // Lines evicted from history no longer exist; copy nothing for them.
        if (serial < history_first)
            continue;
        const std::size_t from = serial == first.line ? first.col : 0;
        std::size_t to_col = serial == last.line ? last.col + 1 : cols;
        if (to_col > cols)
            to_col = cols;
        const auto at = [&](std::size_t column) noexcept
        {
            return serial < history_end ? model.history_cell(serial, column)
                                        : model.cell(static_cast<int>(serial - history_end),
                                                     static_cast<int>(column));
        };
        std::size_t used = 0;
        for (std::size_t c = 0; c < cols; ++c)
        {
            const auto cp = at(c).codepoints[0];
            if (cp && cp != ' ')
                used = c + 1;
        }
        if (to_col > used)
            to_col = used;
        if (any && !soft_wrapped && size < capacity)
            out[size++] = '\n';
        any = true;
        for (std::size_t c = from; c < to_col; ++c)
        {
            const auto cell = at(c);
            if (!cell.width && c > 0 && at(c - 1).width == 2)
                continue; // The second half of a wide character.
            std::uint32_t cp = cell.codepoints[0];
            if (!cp)
                cp = ' ';
            // UTF-8, keeping combining marks stored with the cell.
            for (std::size_t mark = 0;
                 mark < cell.codepoints.size() && (mark == 0 || cell.codepoints[mark]); ++mark)
            {
                const std::uint32_t value = mark ? cell.codepoints[mark] : cp;
                char encoded[4];
                std::size_t length = 0;
                if (value < 0x80)
                {
                    encoded[0] = static_cast<char>(value);
                    length = 1;
                }
                else if (value < 0x800)
                {
                    encoded[0] = static_cast<char>(0xc0 | (value >> 6));
                    encoded[1] = static_cast<char>(0x80 | (value & 63));
                    length = 2;
                }
                else if (value < 0x10000)
                {
                    encoded[0] = static_cast<char>(0xe0 | (value >> 12));
                    encoded[1] = static_cast<char>(0x80 | ((value >> 6) & 63));
                    encoded[2] = static_cast<char>(0x80 | (value & 63));
                    length = 3;
                }
                else
                {
                    encoded[0] = static_cast<char>(0xf0 | (value >> 18));
                    encoded[1] = static_cast<char>(0x80 | ((value >> 12) & 63));
                    encoded[2] = static_cast<char>(0x80 | ((value >> 6) & 63));
                    encoded[3] = static_cast<char>(0x80 | (value & 63));
                    length = 4;
                }
                if (length > capacity - size)
                    return size; // Never split a character at the end.
                for (std::size_t i = 0; i < length; ++i)
                    out[size++] = encoded[i];
            }
        }
        // A line filled to the last column continues on the next row.
        soft_wrapped = used == cols;
    }
    return size;
}

std::size_t paste_text(std::string_view text, char *out, std::size_t capacity) noexcept
{
    std::size_t size = 0;
    for (char ch : text)
    {
        if (size == capacity)
            break;
        if (ch == '\n' || ch == '\r' || ch == '\t')
            ch = ' ';
        if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) > 126)
            continue;
        out[size++] = ch;
    }
    while (size && out[size - 1] == ' ')
        --size;
    return size;
}

void Clipboard::set(std::string_view text) noexcept
{
    size_ = text.size() < capacity ? text.size() : capacity;
    for (std::size_t i = 0; i < size_; ++i)
        data_[i] = text[i];
}

SettingsButton settings_button_at(int px, int py) noexcept
{
    if (linux_switch.contains(px, py))
        return SettingsButton::toggle_linux;
    if (environments_button.contains(px, py))
        return SettingsButton::environments;
    if (language_button.contains(px, py))
        return SettingsButton::language;
    if (text_size_button.contains(px, py))
        return SettingsButton::text_size;
    if (host_mount_switch.contains(px, py))
        return SettingsButton::toggle_host_mount;
    if (ssh_switch.contains(px, py) || help_toggle.contains(px, py))
        return SettingsButton::toggle_ssh;
    if (ssh_retry.contains(px, py))
        return SettingsButton::restart_ssh;
    if (help_password.contains(px, py))
        return SettingsButton::show_password;
    if (help_retry.contains(px, py))
        return SettingsButton::retry;
    if (help_back.contains(px, py))
        return SettingsButton::back;
    if (help_about.contains(px, py))
        return SettingsButton::about;
    if (speed_down_rect.contains(px, py))
        return SettingsButton::speed_down;
    if (speed_up_rect.contains(px, py))
        return SettingsButton::speed_up;
    return SettingsButton::none;
}

Rect env_button_rect(EnvButton button) noexcept
{
    switch (button)
    {
    case EnvButton::default_base:
        return env_default(0);
    case EnvButton::open_alpine:
        return env_open(1);
    case EnvButton::default_alpine:
        return env_default(1);
    case EnvButton::remove_alpine:
        return env_remove(1);
    case EnvButton::open_ubuntu:
        return env_open(2);
    case EnvButton::default_ubuntu:
        return env_default(2);
    case EnvButton::remove_ubuntu:
        return env_remove(2);
    case EnvButton::grow_1:
        return env_grow(0);
    case EnvButton::grow_2:
        return env_grow(1);
    case EnvButton::grow_4:
        return env_grow(2);
    case EnvButton::toggle_linux:
        return env_linux_switch;
    case EnvButton::back:
        return help_back;
    case EnvButton::none:
        break;
    }
    return {};
}
EnvButton env_button_at(int px, int py) noexcept
{
    for (unsigned i = static_cast<unsigned>(EnvButton::default_base);
         i <= static_cast<unsigned>(EnvButton::back); ++i)
    {
        const auto button = static_cast<EnvButton>(i);
        if (env_button_rect(button).contains(px, py))
            return button;
    }
    return EnvButton::none;
}

MainButton main_button_at(int px, int py, bool keyboard) noexcept
{
    if (settings_label.contains(px, py))
        return MainButton::settings;
    if (keyboard_button.contains(px, py))
        return MainButton::keyboard;
    if (reconnect_button.contains(px, py))
        return MainButton::reconnect;
    if (interrupt_button.contains(px, py))
        return MainButton::interrupt;
    if (!keyboard)
        return MainButton::none;
    if (osk_shift.contains(px, py))
        return MainButton::shift;
    if (osk_delete.contains(px, py))
        return MainButton::del;
    if (osk_run.contains(px, py))
        return MainButton::run;
    return MainButton::none;
}

std::size_t load_speed(const char *path) noexcept
{
    int file = ::open(path, O_RDONLY);
    if (file < 0)
        return default_speed;
    char buffer[16]{};
    auto length = ::read(file, buffer, sizeof(buffer) - 1);
    ::close(file);
    unsigned percent = 0;
    bool digits = false;
    for (long i = 0; i < length && buffer[i] >= '0' && buffer[i] <= '9' && percent < 10000; ++i)
    {
        percent = percent * 10 + static_cast<unsigned>(buffer[i] - '0');
        digits = true;
    }
    return digits ? nearest_speed(percent) : default_speed;
}

bool save_speed(const char *directory, const char *path, std::size_t index) noexcept
{
    if (index >= speeds.size())
        return false;
    if (::mkdir(directory, 0700) < 0 && errno != EEXIST)
        return false;
    char text[16];
    int length = std::snprintf(text, sizeof(text), "%u\n", speeds[index]);
    int file = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file < 0)
        return false;
    bool result = length > 0 && ::write(file, text, static_cast<std::size_t>(length)) == length;
    if (::close(file) < 0)
        result = false;
    return result;
}

void EventLog::open(const char *flag, const char *path) noexcept
{
    if (fd_ >= 0)
        return;
    int file = ::open(flag, O_RDONLY);
    if (file < 0)
        return;
    ::close(file);
    fd_ = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

void EventLog::write_line(const char *text, std::size_t size) noexcept
{
    ++lines_;
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    char line[256];
    int used =
        std::snprintf(line, sizeof(line), "t=%lld.%03ld %.*s\n", static_cast<long long>(now.tv_sec),
                      now.tv_nsec / 1000000, static_cast<int>(size), text);
    if (used > 0)
        (void)::write(fd_, line,
                      static_cast<std::size_t>(used) < sizeof(line) ? static_cast<std::size_t>(used)
                                                                    : sizeof(line) - 1);
}
} // namespace term::ui
