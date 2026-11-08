// PS5Term - GPL-3.0-or-later. libvterm is MIT licensed; see NOTICE.md.
#include "terminal_model.hpp"
#include "terminal_memory.h"
#include "scrollback.hpp"
#include "../third_party/libvterm/include/vterm.h"
#include <algorithm>
#include <cstring>
#include <new>
namespace term
{
namespace
{
bool dimensions(int rows, int cols) noexcept
{
    return rows > 0 && rows <= TerminalModel::max_rows && cols > 0 &&
           cols <= TerminalModel::max_columns;
}
} // namespace
struct TerminalModel::Impl
{
    Scrollback history;
    TerminalMemory *memory = nullptr;
    VTerm *vt = nullptr;
    VTermScreen *screen = nullptr;
    VTermState *state = nullptr;
    std::array<char, output_capacity> output{};
    std::size_t head = 0, size = 0;
    std::array<int, max_rows> dirty_start{}, dirty_end{};
    Impl() noexcept
    {
        dirty_start.fill(max_columns);
    }
    void mark(VTermRect rect) noexcept
    {
        int rows = 0, cols = 0;
        vterm_get_size(vt, &rows, &cols);
        const int begin = std::max(0, rect.start_col), end = std::min(cols, rect.end_col);
        if (begin >= end)
            return;
        for (int row = std::max(0, rect.start_row); row < std::min(rows, rect.end_row); ++row)
        {
            dirty_start[row] = std::min(dirty_start[row], begin);
            dirty_end[row] = std::max(dirty_end[row], end);
        }
    }
    void mark_all() noexcept
    {
        int rows = 0, cols = 0;
        vterm_get_size(vt, &rows, &cols);
        mark({0, rows, 0, cols});
    }
    static int damage(VTermRect rect, void *opaque) noexcept
    {
        static_cast<Impl *>(opaque)->mark(rect);
        return 1;
    }
    static int move_cursor(VTermPos pos, VTermPos old, int visible, void *opaque) noexcept
    {
        auto &self = *static_cast<Impl *>(opaque);
        self.visible = visible;
        self.mark({old.row, old.row + 1, old.col, old.col + 1});
        self.mark({pos.row, pos.row + 1, pos.col, pos.col + 1});
        return 1;
    }
    bool failed = false, alternate = false, visible = true;
    MouseReporting mouse = MouseReporting::none;
    Cell convert(VTermScreenCell source) const noexcept
    {
        Cell result;
        std::copy(std::begin(source.chars), std::end(source.chars), result.codepoints.begin());
        // libvterm marks a wide-cell continuation with UINT32_MAX, width 1.
        // Consumers receive width 0 and no glyph for that trailing cell.
        if (result.codepoints[0] == UINT32_MAX)
            result.codepoints = {};
        else
            result.width = static_cast<unsigned char>(source.width);
        result.default_background = VTERM_COLOR_IS_DEFAULT_BG(&source.bg);
        vterm_screen_convert_color_to_rgb(screen, &source.fg);
        vterm_screen_convert_color_to_rgb(screen, &source.bg);
        result.foreground = {source.fg.rgb.red, source.fg.rgb.green, source.fg.rgb.blue};
        result.background = {source.bg.rgb.red, source.bg.rgb.green, source.bg.rgb.blue};
        result.bold = source.attrs.bold;
        result.italic = source.attrs.italic;
        result.underline = source.attrs.underline;
        result.blink = source.attrs.blink;
        result.reverse = source.attrs.reverse;
        result.conceal = source.attrs.conceal;
        result.strike = source.attrs.strike;
        return result;
    }
    static int history_push(int columns, const VTermScreenCell *cells, void *opaque) noexcept
    {
        auto &self = *static_cast<Impl *>(opaque);
        if (self.failed)
            return 0;
        if (columns < 1 || columns > max_columns)
        {
            self.failed = true;
            return 0;
        }
        std::array<Cell, max_columns> snapshot{};
        for (int col = 0; col < columns; ++col)
            snapshot[col] = self.convert(cells[col]);
        if (!self.history.push(std::span<const Cell>(snapshot.data(), columns)))
        {
            self.failed = true;
            return 0;
        }
        return 1;
    }
    static int history_clear(void *opaque) noexcept
    {
        static_cast<Impl *>(opaque)->history.clear();
        return 1;
    }
    static void emit(const char *bytes, std::size_t count, void *opaque) noexcept
    {
        if (!count)
            return;
        auto &self = *static_cast<Impl *>(opaque);
        if (self.failed)
            return;
        if (count > output_capacity - self.size)
        {
            self.failed = true;
            return;
        }
        std::size_t tail = (self.head + self.size) % output_capacity;
        std::size_t first = std::min(count, output_capacity - tail);
        std::memcpy(self.output.data() + tail, bytes, first);
        std::memcpy(self.output.data(), bytes + first, count - first);
        self.size += count;
    }
    static int property(VTermProp prop, VTermValue *value, void *opaque) noexcept
    {
        auto &self = *static_cast<Impl *>(opaque);
        if (prop == VTERM_PROP_ALTSCREEN)
        {
            self.alternate = value->boolean;
            self.mark_all();
        }
        else if (prop == VTERM_PROP_CURSORVISIBLE)
        {
            self.visible = value->boolean;
            self.mark_all();
        }
        else if (prop == VTERM_PROP_MOUSE)
        {
            switch (value->number)
            {
            case VTERM_PROP_MOUSE_CLICK:
                self.mouse = MouseReporting::click;
                break;
            case VTERM_PROP_MOUSE_DRAG:
                self.mouse = MouseReporting::drag;
                break;
            case VTERM_PROP_MOUSE_MOVE:
                self.mouse = MouseReporting::move;
                break;
            default:
                self.mouse = MouseReporting::none;
                break;
            }
        }
        return 1;
    }
    bool reserve(std::size_t n) const noexcept
    {
        return !failed && n <= output_capacity - size;
    }
    ~Impl()
    {
        terminal_memory_destroy(memory);
    }
};
TerminalModel::TerminalModel(int rows, int columns, std::size_t memory_limit) noexcept
{
    if (!dimensions(rows, columns))
        return;
    impl_ = new (std::nothrow) Impl;
    if (!impl_ || !impl_->history.healthy())
    {
        delete impl_;
        impl_ = nullptr;
        return;
    }
    impl_->memory =
        terminal_memory_create(rows, columns, std::min(memory_limit, max_engine_memory));
    impl_->vt = terminal_memory_vterm(impl_->memory);
    if (!impl_->vt)
    {
        delete impl_;
        impl_ = nullptr;
        return;
    }
    vterm_set_utf8(impl_->vt, 1);
    impl_->state = vterm_obtain_state(impl_->vt);
    // Readable on the dark panel at TV distance. The stock palette's blue (0,0,224)
    // and black are close to invisible against it.
    {
        static constexpr std::uint8_t palette[16][3] = {
            {72, 72, 72},    {224, 85, 97},   {140, 194, 101}, {209, 143, 82},
            {74, 165, 240},  {193, 98, 222},  {66, 179, 194},  {215, 218, 224},
            {107, 114, 128}, {255, 97, 110},  {165, 224, 117}, {240, 164, 93},
            {77, 196, 255},  {222, 115, 255}, {76, 209, 224},  {240, 240, 240}};
        for (int index = 0; index < 16; ++index)
        {
            VTermColor color;
            vterm_color_rgb(&color, palette[index][0], palette[index][1], palette[index][2]);
            vterm_state_set_palette_color(impl_->state, index, &color);
        }
    }
    impl_->screen = vterm_obtain_screen(impl_->vt);
    static const VTermScreenCallbacks callbacks = []
    {
        VTermScreenCallbacks c{};
        c.settermprop = Impl::property;
        c.damage = Impl::damage;
        c.movecursor = Impl::move_cursor;
        c.sb_pushline = Impl::history_push;
        c.sb_clear = Impl::history_clear;
        return c;
    }();
    vterm_screen_set_callbacks(impl_->screen, &callbacks, impl_);
    vterm_screen_set_damage_merge(impl_->screen, VTERM_DAMAGE_SCREEN);
    vterm_output_set_callback(impl_->vt, Impl::emit, impl_);
    impl_->mark_all();
}
TerminalModel::~TerminalModel()
{
    delete impl_;
}
bool TerminalModel::healthy() const noexcept
{
    return impl_ && !impl_->failed;
}
int TerminalModel::rows() const noexcept
{
    int n = 0;
    if (healthy())
        vterm_get_size(impl_->vt, &n, nullptr);
    return n;
}
int TerminalModel::columns() const noexcept
{
    int n = 0;
    if (healthy())
        vterm_get_size(impl_->vt, nullptr, &n);
    return n;
}
std::size_t TerminalModel::feed(std::string_view bytes) noexcept
{
    if (!healthy() || bytes.empty())
        return 0;
    std::size_t n = 0;
    if (!terminal_memory_feed(impl_->memory, bytes.data(), std::min(bytes.size(), feed_budget), &n))
        impl_->failed = true;
    return n;
}
TerminalModel::Cell TerminalModel::cell(int row, int column) const noexcept
{
    Cell result;
    if (!healthy() || row < 0 || column < 0 || row >= rows() || column >= columns())
        return result;
    VTermScreenCell source{};
    if (!vterm_screen_get_cell(impl_->screen, {row, column}, &source))
        return result;
    return impl_->convert(source);
}
TerminalModel::Cursor TerminalModel::cursor() const noexcept
{
    if (!healthy())
        return {};
    VTermPos pos{};
    vterm_state_get_cursorpos(impl_->state, &pos);
    return {pos.row, pos.col, impl_->visible};
}
std::size_t TerminalModel::take_damage(std::span<Damage> destination) noexcept
{
    if (!healthy() || destination.empty())
        return 0;
    std::size_t count = 0;
    for (int row = 0; row < rows() && count < destination.size(); ++row)
    {
        if (impl_->dirty_start[row] >= impl_->dirty_end[row])
            continue;
        destination[count++] = {row, impl_->dirty_start[row], impl_->dirty_end[row]};
        impl_->dirty_start[row] = max_columns;
        impl_->dirty_end[row] = 0;
    }
    return count;
}
bool TerminalModel::alternate_screen() const noexcept
{
    return healthy() && impl_->alternate;
}
std::size_t TerminalModel::history_size() const noexcept
{
    return impl_ ? impl_->history.size() : 0;
}
std::uint64_t TerminalModel::history_first() const noexcept
{
    return impl_ ? impl_->history.first() : 1;
}
std::uint64_t TerminalModel::history_end() const noexcept
{
    return impl_ ? impl_->history.end() : 1;
}
std::size_t TerminalModel::history_columns(std::uint64_t id) const noexcept
{
    return impl_ ? impl_->history.columns(id) : 0;
}
TerminalModel::Cell TerminalModel::history_cell(std::uint64_t id, std::size_t column) const noexcept
{
    return impl_ ? impl_->history.cell(id, column) : Cell{};
}
void TerminalModel::clear_history() noexcept
{
    if (impl_)
        impl_->history.clear();
}
bool TerminalModel::resize(int rows, int columns) noexcept
{
    if (!healthy() || !dimensions(rows, columns))
        return false;
    if (!terminal_memory_resize(impl_->memory, rows, columns))
        impl_->failed = true;
    if (healthy())
    {
        impl_->dirty_start.fill(max_columns);
        impl_->dirty_end.fill(0);
        impl_->mark_all();
    }
    return healthy();
}
bool TerminalModel::key(Key key, unsigned modifiers) noexcept
{
    constexpr VTermKey keys[] = {
        VTERM_KEY_ENTER, VTERM_KEY_TAB,  VTERM_KEY_BACKSPACE, VTERM_KEY_ESCAPE,  VTERM_KEY_UP,
        VTERM_KEY_DOWN,  VTERM_KEY_LEFT, VTERM_KEY_RIGHT,     VTERM_KEY_INS,     VTERM_KEY_DEL,
        VTERM_KEY_HOME,  VTERM_KEY_END,  VTERM_KEY_PAGEUP,    VTERM_KEY_PAGEDOWN};
    auto index = static_cast<unsigned>(key);
    if (!healthy() || index >= std::size(keys) || modifiers > 7 || !impl_->reserve(64))
        return false;
    vterm_keyboard_key(impl_->vt, keys[index], static_cast<VTermModifier>(modifiers));
    return healthy();
}
bool TerminalModel::function_key(unsigned number, unsigned modifiers) noexcept
{
    if (!healthy() || number < 1 || number > 12 || modifiers > 7 || !impl_->reserve(64))
        return false;
    vterm_keyboard_key(impl_->vt, static_cast<VTermKey>(VTERM_KEY_FUNCTION(number)),
                       static_cast<VTermModifier>(modifiers));
    return healthy();
}
bool TerminalModel::text(std::uint32_t codepoint, unsigned modifiers) noexcept
{
    if (!healthy() || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        modifiers > 7 || !impl_->reserve(64))
        return false;
    vterm_keyboard_unichar(impl_->vt, codepoint, static_cast<VTermModifier>(modifiers));
    return healthy();
}
bool TerminalModel::raw(std::string_view bytes) noexcept
{
    if (!healthy() || !impl_->reserve(bytes.size()))
        return false;
    Impl::emit(bytes.data(), bytes.size(), impl_);
    return healthy();
}
bool TerminalModel::paste(std::string_view bytes) noexcept
{
    // Reserve both possible six-byte bracket markers before queuing any bytes.
    if (!healthy() || bytes.size() > output_capacity - 12 || !impl_->reserve(bytes.size() + 12))
        return false;
    vterm_keyboard_start_paste(impl_->vt);
    Impl::emit(bytes.data(), bytes.size(), impl_);
    vterm_keyboard_end_paste(impl_->vt);
    return healthy();
}
TerminalModel::MouseReporting TerminalModel::mouse_reporting() const noexcept
{
    return healthy() ? impl_->mouse : MouseReporting::none;
}
bool TerminalModel::mouse_move(int row, int column, unsigned modifiers) noexcept
{
    if (!healthy() || mouse_reporting() == MouseReporting::none || row < 0 || column < 0 ||
        row >= rows() || column >= columns() || modifiers > 7 || !impl_->reserve(64))
        return false;
    vterm_mouse_move(impl_->vt, row, column, static_cast<VTermModifier>(modifiers));
    return healthy();
}
bool TerminalModel::mouse_button(unsigned button, bool pressed, unsigned modifiers) noexcept
{
    if (!healthy() || mouse_reporting() == MouseReporting::none || button < 1 || button > 5 ||
        (button > 3 && !pressed) || modifiers > 7 || !impl_->reserve(64))
        return false;
    vterm_mouse_button(impl_->vt, static_cast<int>(button), pressed,
                       static_cast<VTermModifier>(modifiers));
    return healthy();
}
bool TerminalModel::focus(bool focused) noexcept
{
    if (!healthy() || !impl_->reserve(64))
        return false;
    if (focused)
        vterm_state_focus_in(impl_->state);
    else
        vterm_state_focus_out(impl_->state);
    return healthy();
}
void TerminalModel::set_default_colors(Rgb fg, Rgb bg) noexcept
{
    if (!healthy())
        return;
    VTermColor vfg, vbg;
    vterm_color_rgb(&vfg, fg.red, fg.green, fg.blue);
    vterm_color_rgb(&vbg, bg.red, bg.green, bg.blue);
    vterm_state_set_default_colors(impl_->state, &vfg, &vbg);
    vterm_screen_set_default_colors(impl_->screen, &vfg, &vbg);
    impl_->mark_all();
}
std::size_t TerminalModel::take_output(std::span<char> bytes) noexcept
{
    if (!impl_ || bytes.empty())
        return 0;
    auto n = std::min(bytes.size(), impl_->size),
         first = std::min(n, output_capacity - impl_->head);
    std::memcpy(bytes.data(), impl_->output.data() + impl_->head, first);
    std::memcpy(bytes.data() + first, impl_->output.data(), n - first);
    impl_->head = (impl_->head + n) % output_capacity;
    impl_->size -= n;
    return n;
}
} // namespace term
