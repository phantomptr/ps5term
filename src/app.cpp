// PS5Term - native terminal UI. GPL-3.0-or-later.
#include "app.hpp"
#include "build_info.hpp"
#include "diagnostics.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
namespace term
{
App app;
namespace
{
using ps5::demo::Color;
constexpr Color rgb(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Color>(0xff000000U | b << 16 | g << 8 | r);
}
constexpr auto bg = rgb(10, 15, 23), surface = rgb(17, 24, 35), edge = rgb(34, 47, 62),
               text = rgb(212, 224, 237), muted = rgb(119, 140, 160), cyan = rgb(89, 229, 206),
               blue = rgb(125, 175, 255);
constexpr auto selection_bg = rgb(38, 72, 112), hover_bg = rgb(48, 64, 84);
constexpr const char *data_directory = "/data/ps5term";
constexpr const char *speed_file = "/data/ps5term/mouse.speed";
constexpr Color palette[] = {muted, rgb(255, 120, 137), cyan, rgb(238, 205, 128),
                             blue,  rgb(199, 153, 245), cyan, text};
constexpr std::string_view lower = "1234567890-=qwertyuiop[]asdfghjkl;'\\zxcvbnm,./` ";
constexpr std::string_view upper = "!@#$%^&*()_+QWERTYUIOP{}ASDFGHJKL:\"|ZXCVBNM<>?~ ";
std::string_view slice(std::string_view s, std::size_t start, std::size_t n)
{
    if (start >= s.size())
        return {};
    return {s.data() + start, n < s.size() - start ? n : s.size() - start};
}
void on_key(Key k, char ch)
{
    app.last_input_ = App::InputKind::keyboard;
    app.key(k, ch);
}
Color blend(Color from, Color to, unsigned alpha)
{
    auto a = static_cast<std::uint32_t>(from), b = static_cast<std::uint32_t>(to);
    std::uint32_t out = 0xff000000U;
    for (unsigned shift = 0; shift < 24; shift += 8)
    {
        unsigned x = (a >> shift) & 255, y = (b >> shift) & 255;
        out |= ((x * (255 - alpha) + y * alpha) / 255) << shift;
    }
    return static_cast<Color>(out);
}
unsigned to_unsigned(int value)
{
    return value < 0 ? 0 : static_cast<unsigned>(value);
}
void on_raw_keyboard(std::uint32_t mods, const std::uint16_t *keys, int count, bool connected,
                     bool intercepted)
{
    app.research_keyboard(mods, keys, count, connected, intercepted);
}
} // namespace
void App::welcome() noexcept
{
    text_style_ = settings_.text_style();
    sync_terminal_size();
    ssh_enabled_ = settings_.ssh_enabled();
    speed_ = ui::load_speed(speed_file);
    log_.open("/data/ps5term/mouse.debug", "/data/ps5term/mouse.log");
    log_.line("start speed=%u%%", ui::speeds[speed_]);
    model().set_default_colors({212, 224, 237}, {0, 0, 0});
    model().feed("\033[20h"); // LNM: LF also does CR
    model().feed("PS5Term\nType help for commands. F6 or click Settings; controller: Options.\n\n");
}
void App::submit() noexcept
{
    if (active_connection().native())
    {
        model().key(TerminalModel::Key::enter);
        return;
    }
    if (editor().text() == "clear")
    {
        clear_screen();
        editor().commit();
        return;
    }
    if (research_.queue(editor().text()))
    {
        model().feed(editor().text());
        model().feed("\n");
        editor().commit();
        scroll_ = 0;
        return;
    }
    if (!active_connection().submit(editor().text()))
        return;
    model().feed(editor().text());
    model().feed("\n");
    editor().commit();
    scroll_ = 0;
}
void App::key(Key k, char ch) noexcept
{
    pointer_shown_.other_input(now_);
    if (password_entry_.active())
    {
        password_key(k, ch);
        return;
    }
    // In a running Linux program every F-key is the program's (mc, htop): F2..F6
    // are app keys only elsewhere; Ctrl+Shift+S/K and the buttons still work.
    if ((ch & from_function_key) && program_owns_function_keys())
    {
        const unsigned number = k == Key::reveal_password   ? 2
                                : k == Key::edit_password   ? 3
                                : k == Key::toggle_keyboard ? 4
                                : k == Key::settings        ? 6
                                                            : 0;
        if (number)
        {
            scroll_ = 0;
            model().function_key(number, static_cast<unsigned>(ch) & 7U);
            return;
        }
    }
    if (k == Key::settings)
    {
        if (settings_open_)
            close_settings();
        else
            open_settings();
        return;
    }
    if (settings_open_ && about_open_)
    {
        // About is read-only: every key that means "back" returns to Settings.
        if (k == Key::escape || k == Key::enter || k == Key::backspace ||
            (k == Key::character && ch == ' ') || k == Key::function)
            about_open_ = false;
        return;
    }
    if (settings_open_)
    {
        constexpr ui::SettingsButton actions[] = {
            ui::SettingsButton::toggle_ssh,     ui::SettingsButton::show_password,
            ui::SettingsButton::retry,          ui::SettingsButton::restart_ssh,
            ui::SettingsButton::speed_down,     ui::SettingsButton::speed_up,
            ui::SettingsButton::toggle_linux,   ui::SettingsButton::toggle_host_mount,
            ui::SettingsButton::expand_storage, ui::SettingsButton::text_size,
            ui::SettingsButton::open_alpine,    ui::SettingsButton::back,
            ui::SettingsButton::about};
        if (k == Key::tab || k == Key::down || k == Key::backtab || k == Key::up)
        {
            move_settings_focus((k == Key::backtab || k == Key::up) ? -1 : 1);
            focus_visible_ = true;
        }
        else if (k == Key::escape)
            close_settings();
        else if (k == Key::left || k == Key::right)
            set_speed(k == Key::left ? (speed_ ? speed_ - 1 : 0) : speed_ + 1);
        else if (k == Key::reveal_password)
            settings_action(ui::SettingsButton::show_password);
        else if (k == Key::edit_password)
            begin_password();
        else if (k == Key::enter || (k == Key::character && ch == ' '))
            settings_action(actions[settings_focus_]);
        else if (k == Key::reconnect)
            settings_action(ui::SettingsButton::restart_ssh);
        else if (k == Key::function && (static_cast<unsigned>(ch) & 15U) == 1)
            settings_action(ui::SettingsButton::about); // F1
        return;
    }
    if (k == Key::toggle_split)
    {
        toggle_split();
        return;
    }
    if (k == Key::focus_pane)
    {
        focus_other_pane();
        return;
    }
    if (k == Key::divider_left || k == Key::divider_right)
    {
        if (split_)
        {
            move_divider(split_left_ + (k == Key::divider_left ? -80 : 80));
            return;
        }
        // Unsplit: Ctrl+Shift+Left/Right belongs to the program (modifiers shift|control).
        k = k == Key::divider_left ? Key::left : Key::right;
        ch = 5;
    }
    if (k == Key::new_tab)
    {
        new_tab();
        return;
    }
    if (k == Key::close_tab)
    {
        close_tab();
        return;
    }
    if ((k == Key::next_tab || k == Key::previous_tab) && open_tabs() < 2)
    {
        // One tab: Ctrl+PgUp/PgDn belong to the program (modifier control).
        k = k == Key::next_tab ? Key::pagedown : Key::pageup;
        ch = 4;
    }
    if (k == Key::next_tab)
    {
        cycle_tab(1);
        return;
    }
    if (k == Key::previous_tab)
    {
        cycle_tab(-1);
        return;
    }
    if (k == Key::go_tab)
    {
        const auto index = static_cast<unsigned>(static_cast<unsigned char>(ch));
        if (index < 4 && (!index || tabs_[index - 1]))
            switch_tab(index);
        else
            tab_notice_ = "No such tab; New tab is Ctrl+Shift+T";
        return;
    }
    if (k == Key::select_text)
    {
        if (!search_open_ && !menu_open_)
            menu_action(ui::MenuItem::select_text);
        return;
    }
    if (k == Key::search)
    {
        open_search();
        return;
    }
    if (search_open_)
    {
        search_key(k, ch);
        return;
    }
    if (output_selection_)
    {
        if (k == Key::character && ch == ' ') // Space restarts the selection here.
            selection_.begin(selection_cursor_, selection_cursor_);
        else
            selection_key(k);
        return;
    }
    if (k == Key::menu)
    {
        menu_open_ = !menu_open_;
        menu_keyboard_ = true;
        menu_focus_ = static_cast<unsigned>(ui::menu_items) - 1;
        move_menu_focus(1); // the first item that can be used now
        menu_ = ui::menu_rect(96, 240);
        return;
    }
    if (menu_open_)
    {
        if (k == Key::escape)
            menu_open_ = false;
        else if (k == Key::up || k == Key::backtab || k == Key::down || k == Key::tab)
        {
            menu_keyboard_ = true;
            move_menu_focus((k == Key::up || k == Key::backtab) ? -1 : 1);
        }
        else if (k == Key::enter || (k == Key::character && ch == ' '))
        {
            menu_open_ = false;
            menu_action(static_cast<ui::MenuItem>(menu_focus_));
        }
        return; // A menu owns input; nothing reaches the shell underneath.
    }
    if (k == Key::toggle_keyboard)
    {
        keyboard_ = !keyboard_;
        sync_terminal_size();
        return;
    }
    // Native PTY mode: let the VT model encode application modes and replies.
    if (active_connection().native())
    {
        // Anything sent to the program brings the live screen back into view.
        if (k != Key::pageup && k != Key::pagedown && k != Key::copy && k != Key::menu)
            scroll_ = 0;
        const auto modifiers = static_cast<unsigned>(static_cast<unsigned char>(ch)) & 7U;
        switch (k)
        {
        case Key::tab:
        case Key::backtab:
            if (modifiers & TerminalModel::alt)
                model().raw("\x1b");
            model().key(TerminalModel::Key::tab,
                        k == Key::backtab ? TerminalModel::shift : TerminalModel::none);
            break;
        case Key::character:
            model().text(static_cast<unsigned char>(ch));
            break;
        case Key::enter:
            if (modifiers & TerminalModel::alt)
                model().raw("\x1b");
            model().key(TerminalModel::Key::enter);
            break;
        case Key::backspace:
            // Alt+Backspace deletes a word in readline; Ctrl+Backspace sends ^H.
            if (modifiers & TerminalModel::control)
                model().raw("\x08");
            else
            {
                if (modifiers & TerminalModel::alt)
                    model().raw("\x1b");
                model().key(TerminalModel::Key::backspace);
            }
            break;
        case Key::del:
            model().key(TerminalModel::Key::erase, modifiers);
            break;
        case Key::insert:
            model().key(TerminalModel::Key::insert, modifiers);
            break;
        case Key::left:
            model().key(TerminalModel::Key::left, modifiers);
            break;
        case Key::right:
            model().key(TerminalModel::Key::right, modifiers);
            break;
        case Key::up:
            model().key(TerminalModel::Key::up, modifiers);
            break;
        case Key::down:
            model().key(TerminalModel::Key::down, modifiers);
            break;
        case Key::home:
            model().key(TerminalModel::Key::home, modifiers);
            break;
        case Key::end:
            model().key(TerminalModel::Key::end, modifiers);
            break;
        // Programs such as less use the page keys on the normal screen too, so they
        // get them. Shift+PgUp/PgDn views this terminal's own scrollback.
        case Key::pageup:
            if (modifiers & TerminalModel::shift)
                scroll_ += 12;
            else
            {
                scroll_ = 0;
                model().key(TerminalModel::Key::page_up, modifiers);
            }
            break;
        case Key::pagedown:
            if (modifiers & TerminalModel::shift)
                scroll_ = scroll_ > 12 ? scroll_ - 12 : 0;
            else
            {
                scroll_ = 0;
                model().key(TerminalModel::Key::page_down, modifiers);
            }
            break;
        case Key::control:
            send_control(ch);
            break;
        case Key::alt:
            model().text(static_cast<unsigned char>(ch), TerminalModel::alt);
            break;
        case Key::function: // number | modifiers << 4
            model().function_key(static_cast<unsigned char>(ch) & 0x0fU,
                                 (static_cast<unsigned char>(ch) >> 4) & 7U);
            break;
        case Key::interrupt:
            model().raw("\x03"); // Queued, so it follows keys already typed.
            break;
        case Key::escape:
            selection_.clear();
            model().key(TerminalModel::Key::escape);
            break;
        case Key::clear:
            clear_screen();
            break;
        case Key::copy:
            copy();
            break;
        case Key::paste:
            paste();
            break;
        case Key::reconnect:
            // F5 belongs to the program while it runs. Restarting a live guest would
            // lose unsaved work; only a stopped session restarts from here.
            if (active_connection().failed())
            {
                reset_terminal();
                active_connection().reconnect();
                model().feed("[PS5Term] Reconnecting...\r\n");
            }
            else
                model().function_key(5, static_cast<unsigned>(ch) & 7U);
            break;
        default:
            break;
        }
        return;
    }
    if (k == Key::control)
    {
        switch (ch)
        {
        case 'c':
            this->key(Key::interrupt);
            break;
        case 'l':
            clear_screen();
            break;
        case 'u':
            editor().clear();
            break;
        case 'a':
            editor().home();
            break;
        case 'e':
            editor().end();
            break;
        case 'k':
            while (editor().cursor() < editor().text().size())
                editor().erase();
            break;
        case '[':
            this->key(Key::escape);
            break;
        default:
            break; // Other control codes mean nothing to a line editor.
        }
        return;
    }
    switch (k)
    {
    case Key::tab:
    case Key::backtab:
    case Key::menu:
    case Key::toggle_keyboard:
    case Key::settings:
    case Key::reveal_password:
    case Key::edit_password:
    case Key::control:
    case Key::alt:
    case Key::function:
        break;
    case Key::copy:
        copy();
        break;
    case Key::paste:
        paste();
        break;
    case Key::character:
        editor().insert({&ch, 1});
        break;
    case Key::enter:
        submit();
        break;
    case Key::backspace:
        editor().backspace();
        break;
    case Key::del:
        editor().erase();
        break;
    case Key::left:
        editor().left();
        break;
    case Key::right:
        editor().right();
        break;
    case Key::home:
        editor().home();
        break;
    case Key::end:
        editor().end();
        break;
    case Key::up:
        editor().history(-1);
        break;
    case Key::down:
        editor().history(1);
        break;
    case Key::pageup:
        scroll_ += 12;
        break;
    case Key::pagedown:
        scroll_ = scroll_ > 12 ? scroll_ - 12 : 0;
        break;
    case Key::escape:
        if (selection_.active())
        {
            selection_.clear(); // first Esc drops a mouse selection
            break;
        }
        editor().clear();
        keyboard_ = false;
        break;
    case Key::toggle_split:
    case Key::focus_pane:
    case Key::divider_left:
    case Key::divider_right:
    case Key::new_tab:
    case Key::close_tab:
    case Key::next_tab:
    case Key::previous_tab:
    case Key::select_text:
    case Key::go_tab:
    case Key::insert: // no insert mode in the built-in line editor
        break;
    case Key::search:
        open_search();
        break;
    case Key::interrupt:
        if (active_connection().interrupt())
        {
            editor().clear();
            model().feed("^C\n");
        }
        break;
    case Key::clear:
        clear_screen();
        break;
    case Key::reconnect:
        active_connection().reconnect();
        model().feed("\n[PS5Term] Reconnecting...\n");
        break;
    }
}
void App::tick(std::uint64_t now) noexcept
{
    now_ = now;
    const auto primary_before = model_.history_end();
    connection_.tick(model_, now);
    if (active_tab_ && primary_scroll_ && model_.history_end() > primary_before)
        primary_scroll_ += static_cast<std::size_t>(model_.history_end() - primary_before);
    for (unsigned i = 0; i < tabs_.size(); ++i)
    {
        auto &tab = tabs_[i];
        if (!tab)
            continue;
        const auto before = tab->model.history_end();
        tab->connection.tick(tab->model, now);
        if (active_tab_ != i + 1 && tab->scroll && tab->model.history_end() > before)
            tab->scroll += static_cast<std::size_t>(tab->model.history_end() - before);
    }
    if (search_open_)
    {
        search_.step(model());
        if (!search_selected_ && search_.count())
        {
            search_index_ = 0;
            search_selected_ = true;
            if (!keyboard_)
                reveal_search();
        }
    }
    if (pending_alpine_)
    {
        if (connection_.failed() ||
            connection_.environment() != Connection::Environment::linux_guest)
            pending_alpine_ = false;
        else if (connection_.guest_agent().alive())
        {
            if (!alpine_ready_at_)
                alpine_ready_at_ = now + 1000000;
            if (now >= alpine_ready_at_ && connection_.write_raw("ps5term-alpine open\n", 20))
                pending_alpine_ = false;
        }
    }
    // Keep a scrolled-back view on the same lines while new output arrives.
    if (const auto end = model().history_end(); end != seen_history_end_)
    {
        if (scroll_ && end > seen_history_end_)
            scroll_ += static_cast<std::size_t>(end - seen_history_end_);
        seen_history_end_ = end;
    }
    // A selection of the other screen's text would copy the wrong characters.
    if (const auto alternate = model().alternate_screen(); alternate != was_alternate_)
    {
        was_alternate_ = alternate;
        close_search();
        output_selection_ = false;
        selection_.clear();
        drag_mode_ = 0;
        scroll_ = 0;
    }
    if (ssh_enabled_ || ssh_.uploading())
        ssh_.tick(now);
    if (now >= next_address_)
    {
        update_address();
        next_address_ = now + 5000000;
    }
    tick_diagnostics(now, active_connection().connected(), ssh_enabled_);
    research_.tick(model(), now);
    input_.raw_keyboard = on_raw_keyboard;
    input_.tick(now, on_key);
    keyboard_mods_ = input_.modifiers();
    MouseSample samples[16];
    int count = mouse_.poll(now, samples, 16);
    if (mouse_.status() != mouse_status_)
    {
        mouse_status_ = mouse_.status();
        log_.line("mouse status=0x%08x handle=%d", static_cast<unsigned>(mouse_status_),
                  mouse_.handle());
    }
    for (int i = 0; i < count; ++i)
        mouse(samples[i], now);
    if (mouse_.connected() != mouse_connected_)
    {
        mouse_connected_ = mouse_.connected();
        log_.line("mouse %s", mouse_connected_ ? "connected" : "disconnected");
    }
    // Dragging a selection past the top or bottom of the output scrolls it.
    if (drag_mode_ && !settings_open_ && now >= drag_scroll_at_)
    {
        const auto area = grid().area();
        const bool up = pointer_.y < area.y && scroll_ < max_scroll(),
                   down = pointer_.y >= area.y + area.h && scroll_;
        if (up || down)
        {
            scroll_ = up ? scroll_ + 1 : scroll_ - 1;
            drag_scroll_at_ = now + 60000;
            drag();
        }
    }
    controller(input_.pressed(), input_.held());
    sync_terminal_size();
}
void App::cycle_text_size() noexcept
{
    text_style_ = text_style_ == 3 ? 1 : text_style_ + 1;
    (void)settings_.set_text_style(text_style_);
    output_selection_ = false;
    sync_terminal_size();
}
void App::sync_terminal_size() noexcept
{
    // The OSK only covers rows; geometry changes resize all affected PTYs once.
    for (unsigned i = 0; i < 4; ++i)
    {
        if (i && !tabs_[i - 1])
            continue;
        auto &m = model_at(i);
        const bool pane = split_ && (i == panes_[0] || i == panes_[1]);
        const int rows = (pane ? 564 : 580) / text_metrics::height(text_style_);
        const int cols = std::min(
            TerminalModel::max_columns,
            static_cast<int>(pane_width(i) / static_cast<int>(text_metrics::advance(text_style_))));
        if (m.rows() == rows && m.columns() == cols)
            continue;
        if (!m.resize(rows, cols))
            continue;
        connection_at(i).resize(static_cast<unsigned>(cols), static_cast<unsigned>(rows));
        saved_scroll(i) = 0;
        if (i == active_tab_)
        {
            close_search();
            selection_.clear();
            scroll_ = 0;
            seen_history_end_ = m.history_end();
        }
    }
}
void App::controller(std::uint32_t p, std::uint32_t held) noexcept
{
    if (p)
    {
        pointer_shown_.other_input(now_);
        last_input_ = InputKind::controller;
    }
    if (password_entry_.active())
    {
        password_controller(p);
        return;
    }
    if (p & 0x0008)
    {
        if (settings_open_)
            close_settings();
        else
            open_settings();
        return;
    }
    if (settings_open_ && about_open_)
    {
        if (p & 0x6000) // Circle or Cross
            about_open_ = false;
        return;
    }
    if (settings_open_)
    {
        if (p & 0x2000)
            close_settings();
        if (p & 0x0080)
            set_speed(speed_ ? speed_ - 1 : 0); // D-pad left/right: mouse speed
        if (p & 0x0020)
            set_speed(speed_ + 1);
        if (p & 0x1000)
            password_visible_ = !password_visible_;
        if (p & 0x0010)
            key(Key::up);
        if (p & 0x0040)
            key(Key::down);
        if (p & 0x4000)
            key(Key::enter);
#if defined(PS5TERM_OWNED_SSH)
        if (p & 0x8000)
            begin_password();
#else
        if ((p & 0x8000) && ssh_enabled_)
            ssh_.retry();
#endif
        return;
    }
    if (menu_open_)
    {
        // An open menu owns input; nothing reaches the program underneath.
        if (p & 0x2000)
            menu_open_ = false;
        if (p & 0x0010)
        {
            menu_keyboard_ = true;
            move_menu_focus(-1);
        }
        if (p & 0x0040)
        {
            menu_keyboard_ = true;
            move_menu_focus(1);
        }
        if (p & 0x4000)
        {
            menu_open_ = false;
            menu_action(static_cast<ui::MenuItem>(menu_focus_));
        }
        return;
    }
    if (p & 0x0002) // L3: same actions menu as the keyboard and mouse.
    {
        output_selection_ = false;
        key(Key::menu);
        return;
    }
    if (output_selection_)
    {
        if (p & 0x0010)
            selection_key(Key::up);
        if (p & 0x0040)
            selection_key(Key::down);
        if (p & 0x0080)
            selection_key(Key::left);
        if (p & 0x0020)
            selection_key(Key::right);
        if (p & 0x0400)
            selection_key(Key::pageup);
        if (p & 0x0800)
            selection_key(Key::pagedown);
        if (p & 0x8000)
            selection_.begin(selection_cursor_, selection_cursor_);
        if (p & 0x4000)
            selection_key(Key::enter);
        if (p & 0x2000)
            selection_key(Key::escape);
        return;
    }
    if (search_open_)
    {
        if (p & 0x1000)
            keyboard_ = !keyboard_;
        if (p & 0x2000)
        {
            close_search();
            return;
        }
        if (p & 0x0400)
            search_move(-1);
        if (p & 0x0800)
            search_move(1);
        if (p & 0x0200)
            search_move(1);
        if (keyboard_)
        {
            move_selection(p);
            if (p & 0x0004)
                shift_ = !shift_;
            if (p & 0x4000)
                type_key(selected_);
            if (p & 0x8000)
                search_key(Key::backspace, 0);
        }
        else if (p & 0x4000)
            search_move(1);
        return;
    }
    // Native PTY mode: route controller buttons as raw terminal input.
    if (active_connection().native())
    {
        if (p & 0x1000)
            keyboard_ = !keyboard_; // Triangle: toggle on-screen keyboard
        if (p & 0x2000)
        {
            if (keyboard_)
                keyboard_ = false;
            else if (held & 0x0100)
                model().raw("\x03"); // L2+Circle: Ctrl-C
            else
                model().key(TerminalModel::Key::escape); // Circle: back, as in vi or less
        }
        if (p & 0x8000)
            model().key(TerminalModel::Key::backspace); // Square: backspace
        if (p & 0x0400)
            model().key(TerminalModel::Key::up); // L1: up (history prev)
        if (p & 0x0800)
            model().key(TerminalModel::Key::down); // R1: down (history next)
        if (p & 0x0004)
            shift_ = !shift_; // R3
        if (p & 0x0200)
            model().key(TerminalModel::Key::enter); // R2: enter
        if (keyboard_)
        {
            move_selection(p);
            if (p & 0x4000)
                type_key(selected_);
        }
        else
        {
            // A full-screen program owns the arrows; it has no scrollback to view.
            if (p & 0x0010)
            {
                if (model().alternate_screen())
                    model().key(TerminalModel::Key::up);
                else
                    scroll_ += 3; // D-pad up: scroll
            }
            if (p & 0x0040)
            {
                if (model().alternate_screen())
                    model().key(TerminalModel::Key::down);
                else
                    scroll_ = scroll_ > 3 ? scroll_ - 3 : 0;
            }
            if (p & 0x0080)
                model().key(TerminalModel::Key::left); // D-pad left: cursor left
            if (p & 0x0020)
                model().key(TerminalModel::Key::right); // D-pad right: cursor right
            if (p & 0x4000)
            {
                if (active_connection().failed())
                    key(Key::reconnect);
                else
                    keyboard_ = true; // Cross: open on-screen keyboard
            }
        }
        sync_terminal_size();
        return;
    }
    if (p & 0x1000)
        keyboard_ = !keyboard_; // Triangle
    if (p & 0x2000)
    {
        if (keyboard_)
            keyboard_ = false;
        else if (held & 0x0100)
            key(Key::interrupt);
        else
            editor().clear();
    } // Circle: back/cancel, with L2 as the deliberate interrupt modifier.
    if (p & 0x8000)
        editor().backspace(); // Square
    if (p & 0x0400)
        editor().history(-1); // L1
    if (p & 0x0800)
        editor().history(1); // R1
    if (p & 0x0004)
        shift_ = !shift_; // R3
    if (p & 0x0200)
        submit(); // R2
    if (keyboard_)
    {
        move_selection(p);
        if (p & 0x4000)
            type_key(selected_);
    }
    else
    {
        if (p & 0x0010)
            scroll_ += 3;
        if (p & 0x0040)
            scroll_ = scroll_ > 3 ? scroll_ - 3 : 0;
        if (p & 0x4000)
        {
            if (active_connection().failed())
                key(Key::reconnect);
            else if (!editor().text().empty())
                submit();
            else
                keyboard_ = true;
        }
    }
}
void App::draw(ps5::demo::Canvas &c) noexcept
{
    if (password_entry_.active())
        draw_password(c);
    else if (settings_open_ && about_open_)
        draw_about(c);
    else if (settings_open_)
        draw_settings(c);
    else
    {
        draw_terminal(c);
        if (search_open_)
            draw_search(c);
        if (menu_open_)
            draw_menu(c);
    }
    draw_pointer(c);
}
void App::draw_terminal(ps5::demo::Canvas &c) noexcept
{
    c.clear(bg);
    c.rectangle(64, 52, 72, 56, surface);
    c.text(72, 64, ">_", 3, cyan);
    c.text(160, 66, "PS5Term", 3, text);
    c.text(160, 102, "Your console. Your tools.", 1, muted);
    c.circle(1448, 80, 5, active_connection().connected() ? cyan : rgb(238, 205, 128));
    c.text(1468, 72,
           active_connection().connected() ? "LOCAL / CONNECTED"
           : active_connection().failed()  ? "LOCAL / OFFLINE"
                                           : "LOCAL / STARTING",
           2, muted);
    c.rectangle(64, 140, 1792, 2, edge);
    draw_tabs(c);
    c.text(1160, 78,
           ssh_enabled_ ? (ssh_.listening() ? "SSH ON"
                           : ssh_.failed()  ? "SSH ERROR"
                                            : "SSH STARTING")
                        : "SSH OFF",
           1, ssh_enabled_ && ssh_.listening() ? cyan : muted);
    if (mouse_connected_)
    {
        c.circle(1160, 110, 4, cyan);
        c.text(1172, 106, "MOUSE", 1, cyan);
    }
    // Labels name the keys of the input in use; in a Linux program F-keys are its own.
    const bool program_keys = program_owns_function_keys();
    label_button(c, ui::keyboard_button,
                 pick(program_keys ? "Keyboard (Ctrl+Shift+K)" : "Keyboard (F4)", "Keyboard",
                      "Keyboard (Triangle)"),
                 1, keyboard_ ? cyan : muted);
    label_button(c, ui::reconnect_button,
                 pick(program_keys ? "Reconnect" : "Reconnect (F5)", "Reconnect", "Reconnect"), 1,
                 muted);
    label_button(c, ui::interrupt_button,
                 pick("Interrupt (Ctrl+C)", "Interrupt", "Interrupt (L2+Circle)"), 1, muted);
    if (hover(ui::settings_label))
        c.rectangle(1472, 158, 160, 24, hover_bg);
    c.text(1480, 166,
           pick(program_keys ? "Settings (Ctrl+Shift+S)" : "Settings (F6)", "Settings",
                "Settings (Options)"),
           1, hover(ui::settings_label) ? text : muted);
    c.rectangle(64, 202, 1792, keyboard_ ? 420 : 626, surface);
    if (split_)
    {
        const bool dragging = divider_drag_ >= 0;
        const int divider = dragging ? divider_drag_ : ui::divider_x(split_left_);
        const bool hot =
            dragging || (pointer_shown_.alpha(now_) && std::abs(pointer_.x - divider) <= 12 &&
                         pointer_.y >= 202 && pointer_.y < 202 + (keyboard_ ? 420 : 626));
        c.rectangle(to_unsigned(divider - (hot ? 2 : 0)), 202, hot ? 6 : 2, keyboard_ ? 420 : 626,
                    hot ? cyan : edge);
        for (auto index : panes_)
        {
            const auto g = grid_for(index);
            const auto color = index == active_tab_ ? cyan : muted;
            c.rectangle(to_unsigned(g.left - 12), 202, to_unsigned(pane_width(index) + 28), 2,
                        color);
            char title[64];
            std::snprintf(title, sizeof title, "Tab %u%s", index + 1,
                          index == active_tab_ ? " / INPUT" : " / click to focus");
            c.text(g.left, 210, title, 1, color);
            draw_output(c, index);
        }
    }
    else
        draw_output(c, active_tab_);
    if (scroll_)
        c.text(1500, keyboard_ ? 592 : 794, "SCROLLBACK", 1, cyan);
    if (keyboard_)
    {
        c.text(84, 610, "KEYBOARD", 1, muted);
        label_button(c, ui::osk_shift, "Shift", 1, shift_ ? text : muted);
        label_button(c, ui::osk_delete, "Delete", 1, muted);
        label_button(c, ui::osk_run, search_open_ ? "Next" : "Run", 1, muted);
        auto keys = shift_ ? upper : lower;
        static constexpr std::string_view terminal_keys[] = {
            "Esc",  "Tab",  "Ctrl", "Alt", "Left", "Down", "Up",  "Right",
            "PgUp", "PgDn", "Home", "End", "F1",   "F2",   "F3",  "F4",
            "F5",   "F6",   "F7",   "F8",  "F9",   "F10",  "Del", "Ins"};
        for (unsigned i = 0; i < ui::keyboard_keys; ++i)
        {
            const auto rect = ui::key_rect(i);
            const unsigned x = to_unsigned(rect.x), y = to_unsigned(rect.y);
            // A latched modifier stays lit until the next key uses it.
            const bool latched = (i == 50 && (osk_modifiers_ & TerminalModel::control)) ||
                                 (i == 51 && (osk_modifiers_ & TerminalModel::alt));
            c.rectangle(x, y, 135, 30,
                        i == selected_ ? cyan
                        : latched      ? edge
                        : hover(rect)  ? hover_bg
                                       : surface);
            if (i < keys.size())
                c.text(x + 53, y + 7, keys[i] == ' ' ? "_" : slice(keys, i, 1), 2,
                       i == selected_ ? bg : text);
            else if (i >= ui::character_keys)
            {
                const auto label = terminal_keys[i - ui::character_keys];
                c.text(x + (135 - static_cast<unsigned>(label.size()) *
                                      text_metrics::advance(text_style_)) /
                               2,
                       y + 7, label, 2,
                       i == selected_ ? bg
                       : latched      ? cyan
                                      : muted);
            }
        }
    }
    if (!active_connection().native())
    {
        c.rectangle(64, 850, 1792, 76, edge);
        c.rectangle(66, 852, 1788, 72, bg);
        c.text(88, 878, ">", 2, cyan);
        constexpr auto input_window = text_metrics::input_columns - text_metrics::input_margin;
        std::size_t offset =
            editor().cursor() > input_window ? editor().cursor() - input_window : 0;
        auto input = slice(editor().text(), offset, text_metrics::input_columns);
        c.text(128, 878, input, 2, text);
        if ((now_ / 500000) % 2 == 0)
            c.rectangle(128 + static_cast<unsigned>(editor().cursor() - offset) *
                                  text_metrics::cell_width,
                        876, 2, text_metrics::height(text_style_), cyan);
    }
    if (output_selection_)
        c.text(84, 951,
               pick("Select: arrows/PgUp/PgDn move; Space starts here; Enter copies; Esc cancels",
                    "Select: drag with the mouse, or arrows; Enter copies; Esc cancels",
                    "Select: D-pad/L1/R1 move; Square starts here; Cross copies; Circle cancels"),
               1, cyan);
    else if (now_ < reconnect_armed_until_)
        c.text(84, 951,
               "Click Reconnect again to restart Linux. Unsaved files outside /mnt/data are lost.",
               1, rgb(238, 205, 128));
    else
        c.text(84, 951, active_connection().status(), 1,
               active_connection().connected() ? cyan : muted);
    c.text(84, 987,
           pick(program_keys ? "Enter Run   Ctrl+C Interrupt   Shift+F10 Actions   Ctrl+Shift+K "
                               "Keyboard   Ctrl+Shift+S Settings"
                             : "Enter Run   Ctrl+C Interrupt   Shift+F10 Actions   F4 Keyboard   "
                               "F5 Reconnect   F6 Settings",
                "Click a button; right-click for Copy / Paste / Select / Find / tabs",
                "Triangle Keyboard   R2 Run   L2+Circle Interrupt   L3 Actions   Options Settings"),
           2, muted);
    c.text(84, 1023,
           pick(program_keys
                    ? "F1-F12 go to the program. Ctrl+Shift+Space select, Ctrl+Shift+V paste, "
                      "Ctrl+Shift+F find, Ctrl+Shift+T/W/1-4 tabs, Shift+PgUp/PgDn scroll back."
                    : "Ctrl+Shift+Space select text, Ctrl+Shift+V paste, Ctrl+Shift+F find, "
                      "Ctrl+Shift+T/W/1-4 tabs, PgUp/PgDn scroll back.",
                "Drag to select, middle-click to paste, wheel to scroll (Shift for pages). In "
                "programs that use the mouse, hold Shift or use Select text from the menu.",
                "Keyboard: D-pad moves, Cross types, R3 Shift, Square Backspace; last row has "
                "F1-F10, Del, Ins. Keyboard closed: D-pad scrolls."),
           1, muted);
}
void App::draw_output(ps5::demo::Canvas &c, unsigned index) noexcept
{
    auto &m = model_at(index);
    auto &connection = connection_at(index);
    const bool focused = index == active_tab_;
    auto &scroll = focused ? scroll_ : saved_scroll(index);
    const auto g = grid_for(index);
    const std::size_t visible = g.rows;
    const auto history = m.history_size();
    const auto rows = static_cast<std::size_t>(m.rows());
    const auto last = std::min(
        rows,
        visible < rows ? std::max(visible, static_cast<std::size_t>(m.cursor().row) + 1) : rows);
    const auto end = history + last;
    const auto maximum = m.alternate_screen() ? 0 : (end > visible ? end - visible : 0);
    scroll = std::min(scroll, maximum);
    const std::size_t view_end = end > scroll ? end - scroll : 0;
    const std::size_t view_start = view_end > visible ? view_end - visible : 0;
    const auto history_first = m.history_first();
    const auto history_end_id = m.history_end();
    const auto cols = g.columns;
    for (std::size_t vi = 0; vi < visible && view_start + vi < view_end; ++vi)
    {
        const std::size_t li = view_start + vi;
        const std::uint64_t serial =
            li < history ? history_first + li : history_end_id + (li - history);
        for (std::size_t col = 0; col < cols; ++col)
        {
            TerminalModel::Cell cell;
            if (li < history)
                cell = m.history_cell(history_first + li, col);
            else
                cell = m.cell(static_cast<int>(li - history), static_cast<int>(col));
            const bool found = focused && search_open_ && search_selected_ &&
                               search_index_ < search_.count() &&
                               search_.match(search_index_).line == serial &&
                               col >= search_.match(search_index_).first &&
                               col <= search_.match(search_index_).last;
            const auto cp = cell.codepoints[0];
            if (cp == 0 && cell.width == 0)
                continue;
            const unsigned x = g.left +
                               static_cast<unsigned>(col) * text_metrics::advance(text_style_),
                           y = g.top +
                               static_cast<unsigned>(vi) * text_metrics::height(text_style_);
            auto fg_rgb = cell.foreground;
            auto bg_rgb = cell.background;
            if (cell.reverse)
                std::swap(fg_rgb, bg_rgb);
            if (found)
            {
                bg_rgb = {90, 65, 15};
                fg_rgb = {255, 240, 170};
                cell.default_background = false;
            }
            if (cell.reverse || !cell.default_background)
                c.rectangle(
                    x, y, text_metrics::advance(text_style_) * (cell.width ? cell.width : 1),
                    text_metrics::height(text_style_), rgb(bg_rgb.red, bg_rgb.green, bg_rgb.blue));
            if (focused && selection_.contains(serial, col))
                c.rectangle(x, y, text_metrics::advance(text_style_),
                            text_metrics::height(text_style_), selection_bg);
            auto fg = rgb(fg_rgb.red, fg_rgb.green, fg_rgb.blue);
            if (cell.bold)
                fg = rgb(std::min(255u, static_cast<unsigned>(fg_rgb.red) + 40),
                         std::min(255u, static_cast<unsigned>(fg_rgb.green) + 40),
                         std::min(255u, static_cast<unsigned>(fg_rgb.blue) + 40));
            if (cp >= 0x20 && cp <= 0x10ffff && cp != 0x7f && cell.width > 0 && !cell.conceal)
                c.glyph(x, y, cp, fg, text_style_);
            const auto span = text_metrics::advance(text_style_) * (cell.width ? cell.width : 1);
            if (cell.underline && !cell.conceal)
                c.rectangle(x, y + text_metrics::height(text_style_) - 3, span,
                            cell.underline == 2 ? 2 : 1, fg);
            if (cell.strike && !cell.conceal)
                c.rectangle(x, y + text_metrics::height(text_style_) / 2, span, 1, fg);
        }
    }
    // The built-in shell draws its own caret in the input box.
    if (focused && connection.native())
    {
        auto cur = m.cursor();
        if (cur.visible && cur.column < static_cast<int>(g.columns) && (now_ / 500000) % 2 == 0)
        {
            const std::size_t row = history + static_cast<std::size_t>(cur.row);
            if (row >= view_start && row < view_end)
            {
                const unsigned cx =
                    g.left + static_cast<unsigned>(cur.column) * text_metrics::advance(text_style_);
                const unsigned cy = g.top + static_cast<unsigned>(row - view_start) *
                                                text_metrics::height(text_style_);
                c.rectangle(cx, cy, 2, text_metrics::height(text_style_), cyan);
            }
        }
    }
}
void App::toggle_ssh() noexcept
{
    bool enabled = !ssh_enabled_;
    if (!settings_.set_ssh_enabled(enabled))
    {
        settings_error_ = true;
        model().feed("\n[Settings] Cannot save SSH preference in /data/ps5term.\n");
        return;
    }
    settings_error_ = false;
    ssh_enabled_ = enabled;
    if (enabled)
        ssh_.retry();
    else
        ssh_.cancel();
    // Settings shows the state; never write into a running program's screen.
    if (!active_connection().native())
        model().feed(enabled ? "\n[Settings] SSH enabled.\n"
                             : "\n[Settings] SSH disabled; server sessions will disconnect.\n");
}
void App::update_address() noexcept
{
    address_[0] = 0;
    // getifaddrs is not exported by the bundled libc.prx. A connected UDP socket
    // reports the default-route interface address without sending any packet.
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return;
    sockaddr_in probe{};
    probe.sin_family = AF_INET;
    probe.sin_port = htons(9);
    probe.sin_addr.s_addr = htonl(0x08080808);
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&probe), sizeof(probe)) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &size) == 0)
    {
        auto value = ntohl(local.sin_addr.s_addr);
        if (value != 0 && (value >> 24) != 127)
            std::snprintf(address_.data(), address_.size(), "%u.%u.%u.%u", value >> 24,
                          (value >> 16) & 255, (value >> 8) & 255, value & 255);
    }
    ::close(fd);
}
void App::draw_settings(ps5::demo::Canvas &c) noexcept
{
    c.clear(bg);
    c.text(84, 70, ">_", 3, cyan);
    c.text(174, 70, "PS5Term / Settings", 3, text);
    c.text(84, 140, "Optional services for your research workflow.", 2, muted);
    c.rectangle(64, 208, 1792, 420, surface);
    c.text(96, 246, "SSH SERVER", 2, text);
    label_button(c, ui::ssh_retry, pick("Retry SSH (F5)", "Retry SSH", "Retry SSH"), 1,
                 ssh_enabled_ ? text : muted);
    c.rectangle(1580, 234, 224, 52,
                ssh_enabled_                                         ? cyan
                : (hover(ui::ssh_switch) || focused(ui::ssh_switch)) ? hover_bg
                                                                     : edge);
    c.text(1630, 252, ssh_enabled_ ? "ON" : "OFF", 2, ssh_enabled_ ? bg : text);
#if defined(PS5TERM_OWNED_SSH)
#if defined(PS5TERM_OWNED_SFTP)
    c.text(
        96, 296,
        "App-owned SSH and SFTP. ps5term@ is the built-in shell; linux@ opens a real Linux shell.",
        2, muted);
#else
    c.text(96, 296, "App-owned SSH. Basic commands; full shell and SFTP are in development.", 2,
           muted);
#endif
    constexpr const char *ssh_user = "ps5term";
#else
    c.text(96, 296, "Connect from your computer. Run commands and transfer files with SFTP.", 2,
           muted);
    constexpr const char *ssh_user = "root";
#endif
    c.text(96, 340,
           settings_error_
               ? "Cannot save SSH setting. Check access to /data/ps5term."
               : (ssh_enabled_ ? ssh_.status() : "Disabled. Activate Enable SSH to start."),
           2, settings_error_ ? palette[1] : (ssh_enabled_ && ssh_.listening() ? cyan : muted));
    if (ssh_enabled_ && ssh_.listening())
    {
        std::array<char, 128> command{};
        auto host = address_[0] ? address_.data() : "<PS5-IP>";
#if defined(PS5TERM_OWNED_SSH)
        std::snprintf(command.data(), command.size(), "command ssh -T -p %u %s@%s", ssh_.port(),
                      ssh_user, host);
#else
        if (ssh_.port() == 22)
            std::snprintf(command.data(), command.size(), "ssh %s@%s", ssh_user, host);
        else
            std::snprintf(command.data(), command.size(), "ssh -p %u %s@%s", ssh_.port(), ssh_user,
                          host);
#endif
        c.text(96, 380, command.data(), 3, cyan);
#if defined(PS5TERM_OWNED_SSH)
        if (Connection::linux_available())
        {
            std::snprintf(command.data(), command.size(), "Linux shell: ssh -p %u linux@%s",
                          ssh_.port(), host);
            c.text(96, 466, command.data(), 2, text);
        }
#if defined(PS5TERM_OWNED_SFTP)
        std::snprintf(command.data(), command.size(), "sftp -P %u %s@%s", ssh_.port(), ssh_user,
                      host);
        c.text(96, 430, command.data(), 2, text);
#else
        c.text(96, 430, "Plain SSH: command ssh -T -p 2222 ps5term@<PS5-IP>", 2, text);
#endif
#else
        if (ssh_.port() == 22)
            std::snprintf(command.data(), command.size(), "sftp root@%s", host);
        else
            std::snprintf(command.data(), command.size(), "sftp -P %u root@%s", ssh_.port(), host);
        c.text(96, 430, command.data(), 2, text);
#endif
    }
    else
        c.text(96, 380,
               ssh_enabled_ ? "SSH is enabled. Check the service status above."
                            : "SSH is off. Your preference is saved on this console.",
               2, text);
    if (password_visible_)
    {
        // Once per reveal, not once per frame: this is a file open and read.
        if (!password_loaded_)
        {
            password_available_ =
                Settings::read_ssh_password(password_cache_.data(), password_cache_.size());
            password_loaded_ = true;
        }
        c.text(96, 502,
               password_available_ ? "Password:"
                                   : "Password unavailable. Enable SSH to create one.",
               2, muted);
        if (password_available_)
            c.text(270, 502, slice(password_cache_.data(), 0, 94), 2, cyan);
    }
    else
    {
        if (password_loaded_)
        {
            volatile char *wipe = password_cache_.data();
            for (std::size_t i = 0; i < password_cache_.size(); ++i)
                wipe[i] = 0;
            password_loaded_ = false;
        }
        c.text(96, 502,
               pick("Password: hidden. Use Show password or F2.",
                    "Password: hidden. Click Show password.",
                    "Password: hidden. Triangle shows it."),
               2, muted);
    }
#if defined(PS5TERM_OWNED_SSH)
    c.text(96, 538, "Public keys: /data/ps5term/ssh/authorized_keys", 2, muted);
    c.text(96, 574, "Over SSH: ssh-key-add <public-key> / ssh-key-remove <public-key>", 2, muted);

#else
    c.text(96, 538, "Public keys: /data/ps5sshd/authorized_keys", 2, muted);
    c.text(96, 574, "Tools: /data/ps5sshd/bin", 2, muted);

#endif
    const bool linux = connection_.environment() == Connection::Environment::linux_guest;
    c.rectangle(64, 644, 1792, 204, surface);
    c.text(96, 668, "LINUX SUBSYSTEM / EXPERIMENTAL", 2, text);
    label_button(c, ui::linux_switch,
                 !Connection::linux_available() ? "Unavailable in this build"
                 : linux                        ? "Stop Linux / return to PS5"
                                                : "Start Linux",
                 2, Connection::linux_available() ? cyan : muted);
    const bool shares = platform::LinuxSubsystem::shared_folder_available();
    if (shares && Connection::linux_available())
        label_button(c, ui::host_mount_switch,
                     host_mount_whole_ ? "PS5 files in /mnt/ps5: / (all the app sees)"
                                       : "PS5 files in /mnt/ps5: /data",
                     2, host_mount_whole_ ? palette[1] : cyan);
    if (host_mount_error_)
        c.text(96, 772, "Cannot save the /mnt/ps5 choice. Check access to /data/ps5term.", 2,
               palette[1]);
    else if (shares && host_mount_whole_)
        c.text(96, 772,
               linux ? "/ takes effect at the next Linux start. Linux root can then change or "
                       "delete whatever this app can reach."
                     : "/ lets Linux root change or delete whatever this app can reach, except "
                       "PS5Term's SSH keys and Linux disks.",
               2, palette[1]);
    else
        c.text(
            96, 772,
            platform::LinuxSubsystem::network_available()
                ? "Outbound IPv4 NAT: eth0 by DHCP, DNS 1.1.1.1 or /data/ps5term/linux/resolver. "
                  "/mnt/data saved; /mnt/ps5 is the PS5's /data. Root temporary."
            : shares
                ? (platform::LinuxSubsystem::persistence_available()
                       ? "/mnt/data saved; /mnt/ps5 is the PS5's /data. Root temporary; no "
                         "network. SSH stays native."
                       : "/mnt/ps5 is the PS5's /data; system temporary. No network. SSH stays "
                         "native.")
            : platform::LinuxSubsystem::persistence_available()
                ? "/mnt/data is saved; system files are temporary. No network/share. SSH stays "
                  "native."
                : "Isolated RISC-V Linux. Temporary disk; no network or PS5 files. SSH stays "
                  "native.",
            2, muted);
    char storage_label[128];
    if (connection_.storage_busy())
        std::snprintf(storage_label, sizeof(storage_label),
                      "Expanding storage: %u%% (keep app open)", connection_.storage_progress());
    else if (storage_requested_ && connection_.storage_error())
        std::snprintf(storage_label, sizeof(storage_label),
                      "Expansion failed (%d): backup kept if created", connection_.storage_error());
    else if (storage_requested_)
        std::snprintf(storage_label, sizeof(storage_label),
                      "512 MiB ready; start Linux to finish filesystem growth");
    else
        std::snprintf(storage_label, sizeof(storage_label),
                      "Expand data to 512 MiB (keeps backup)");
    char link_failure[160] = "";
    if (const char *why = connection_.guest_agent().failure())
        std::snprintf(link_failure, sizeof(link_failure),
                      "SSH link to Linux failed (%s). Stop Linux and start it again for linux@.",
                      why);
    label_button(c, ui::alpine_button, "Open / install Alpine", 2, cyan);
    label_button(c, ui::storage_expand, storage_label, 2, cyan);
    const bool armed = now_ < armed_until_;
    c.text(96, 808,
           armed && armed_button_ == ui::SettingsButton::toggle_linux
               ? "Press Stop Linux again to stop: Linux shells and linux@ SSH sessions end."
           : armed && armed_button_ == ui::SettingsButton::expand_storage
               ? "Press Expand again to grow the Linux data disk to 512 MiB (keeps a backup)."
           : storage_result_[0]       ? storage_result_
           : linux && link_failure[0] ? link_failure
           : linux                    ? connection_.status()
                                      : "Linux stopped. Disk backup: linux/data.ext2.backup",
           2, armed ? rgb(238, 205, 128) : muted);
    // Mouse pointer speed (D-pad / arrow keys, or click - and +).
    c.rectangle(64, 864, 1792, 56, surface);
    label_button(c, ui::text_size_button,
                 text_style_ == 1   ? "Text: small"
                 : text_style_ == 2 ? "Text: normal"
                                    : "Text: large",
                 1, cyan);
    c.text(96, 882, "MOUSE SPEED", 2, text);
    c.text(316, 886, pick("Left / Right to change", "Click - or +", "D-pad Left / Right to change"),
           1, muted);
    char speed[16];
    std::snprintf(speed, sizeof(speed), "%u.%02ux", ui::speeds[speed_] / 100,
                  ui::speeds[speed_] % 100);
    for (const auto &r : {ui::speed_down_rect, ui::speed_up_rect})
    {
        c.rectangle(to_unsigned(r.x), to_unsigned(r.y), to_unsigned(r.w), to_unsigned(r.h),
                    (hover(r) || focused(r)) ? hover_bg : edge);
        c.text(to_unsigned(r.x + 28), to_unsigned(r.y + 14), r.x == ui::speed_up_rect.x ? "+" : "-",
               2, text);
    }
    c.text(1544, 882, speed, 2, cyan);
    label_button(c, ui::help_toggle, "Enable / disable SSH", 2, cyan);
    label_button(c, ui::help_password,
                 pick("Show / hide password (F2)", "Show / hide password",
                      "Show / hide password (Triangle)"),
                 2, muted);
#if defined(PS5TERM_OWNED_SSH)
    label_button(c, ui::help_retry,
                 pick("Change password (F3)", "Change password", "Change password (Square)"), 2,
                 cyan);
    if (*password_result_)
        c.text(1040, 980, password_result_, 2, cyan);
#else
    label_button(c, ui::help_retry, "Retry SSH (F5)", 2, muted);
#endif
    label_button(c, ui::help_back,
                 pick("Back to terminal (Esc)", "Back to terminal", "Back to terminal (Circle)"), 2,
                 muted);
    label_button(c, ui::help_about, pick("About PS5Term (F1)", "About PS5Term", "About PS5Term"), 2,
                 muted);
    c.text(
        84, 1056,
        pick("Tab / Shift+Tab or Up / Down: focus   Enter / Space: activate   Esc: back",
             "Click a control; right-click goes back",
             "D-pad Up / Down: focus   Cross: activate   Circle: back   Triangle: show password   "
             "Square: change password"),
        1, muted);
}

namespace
{
// Canvas::text is ASCII; names such as Törnblom need the glyph atlas for the rest.
void credit_text(ps5::demo::Canvas &c, unsigned x, unsigned y, std::string_view value,
                 unsigned style, Color color) noexcept
{
    const unsigned width = text_metrics::advance(style);
    for (std::size_t i = 0; i < value.size();)
    {
        const auto byte = static_cast<unsigned char>(value[i]);
        std::uint32_t code = byte;
        std::size_t length = 1;
        if ((byte & 0xe0U) == 0xc0U && i + 1 < value.size())
        {
            code = (byte & 0x1fU) << 6 | (static_cast<unsigned char>(value[i + 1]) & 0x3fU);
            length = 2;
        }
        if (code < 128)
            c.text(x, y, value.substr(i, 1), style, color);
        else
            c.glyph(x, y, code, color, style); // Draws the replacement mark if missing.
        x += width;
        i += length;
    }
}
} // namespace

// About: who made PS5Term, what it is built on, and the disclaimer. The console has no
// browser, so links are written out to type on a computer or phone.
void App::draw_about(ps5::demo::Canvas &c) noexcept
{
    c.clear(bg);
    c.text(84, 70, ">_", 3, cyan);
    c.text(174, 70, "PS5Term / About", 3, text);
    c.text(84, 140,
           "A terminal for a jailbroken PS5: a native shell, a real Linux shell, and SSH and SFTP "
           "from your computer.",
           2, muted);
    char line[160];
    std::snprintf(line, sizeof(line), "Version %s    License GPL-3.0-or-later", version);
    c.text(84, 180, line, 2, cyan);
    std::snprintf(line, sizeof(line), "Build %.64s", build_id);
    c.text(84, 214, line, 1, muted);

    c.rectangle(64, 252, 1792, 196, surface);
    c.text(96, 280, "CREATED BY", 2, text);
    c.text(96, 318, "PhantomPtr", 3, cyan);
    c.text(276, 324, "phantomptr.com      Also by PhantomPtr: PS5Upload", 2, muted);
    c.text(96, 370,
           "X  @phantomptr      Discord  discord.gg/fzK3xddtrM      Email  phantomptr@gmail.com", 2,
           text);
    c.text(96, 406,
           "Website  ps5term.phantomptr.com      Source  github.com/phantomptr/ps5term      "
           "Support  ko-fi.com/B0B81S0WUA",
           2, text);

    c.rectangle(64, 468, 1792, 300, surface);
    c.text(96, 496, "BUILT ON", 2, text);
    constexpr std::string_view credits[] = {
        "App foundation, icon and music: ps5-native-app-boilerplate by BlackBearReloaded",
        "PS5 Payload SDK and shsrv by John Törnblom (ps5-payload-dev)    ps5sshd by intestinate",
        "SSH and SFTP: wolfSSH and wolfSSL    Terminal: libvterm by Paul Evans",
        "Linux: TinyEMU by Fabrice Bellard, libslirp (QEMU project), Buildroot, Linux, OpenSBI, "
        "musl, "
        "BusyBox; optional Alpine Linux",
        "Fonts: JetBrains Mono (JetBrains), font8x8 (Daniel Hepper)    Research notes: kstuff "
        "contributors, kodi-ps5",
        "Each project keeps its own license. Full credits and notices: NOTICE.md in the source.",
    };
    unsigned y = 534;
    for (const auto credit : credits)
    {
        credit_text(c, 96, y, credit, 2, muted);
        y += 32;
    }
    c.text(96, 732,
           "PS5Term is one tool among many. If something here helps you, please support the "
           "people behind the PS5 scene's tools too.",
           1, muted);

    c.rectangle(64, 788, 1792, 200, surface);
    c.text(96, 816, "DISCLAIMER", 2, text);
    constexpr std::string_view disclaimer[] = {
        "For research, education, interoperability and homebrew development. Not affiliated with "
        "or endorsed by Sony Interactive Entertainment.",
        "\"PlayStation\" and \"PS5\" are trademarks of Sony Interactive Entertainment Inc., used "
        "here for identification only.",
        "Provided as is, without warranty. A modified console can be damaged, lose data or be "
        "banned. Use it at your own risk,",
        "only with hardware and content you own. Full terms: DISCLAIMER.md in the source.",
    };
    y = 852;
    for (const auto line_text : disclaimer)
    {
        c.text(96, y, line_text, 2, muted);
        y += 32;
    }

    label_button(c, ui::help_back,
                 pick("Back to Settings (Esc)", "Back to Settings", "Back to Settings (Circle)"), 2,
                 cyan);
    c.text(1625, 1028, "Made with love for the PS5 scene.", 1, muted);
    c.text(84, 1056,
           pick("Esc / Enter: back   F6: close Settings", "Click Back or right-click to go back",
                "Circle / Cross: back   Options: close Settings"),
           1, muted);
}

// ---- Mouse -----------------------------------------------------------------

ui::Grid App::grid_for(unsigned index) const noexcept
{
    ui::Grid g;
    const auto &m = model_at(index);
    const bool pane = split_ && (index == panes_[0] || index == panes_[1]);
    g.left = pane && index == panes_[1] ? 96 + split_left_ + 68 : 96;
    g.top = pane ? 238 : 222;
    g.cell_width = static_cast<int>(text_metrics::advance(text_style_));
    g.row_height = static_cast<int>(text_metrics::height(text_style_));
    g.rows = keyboard_ ? std::min(static_cast<std::size_t>(m.rows()),
                                  static_cast<std::size_t>((pane ? 360 : 380) / g.row_height))
                       : static_cast<std::size_t>(m.rows());
    g.columns = std::min(static_cast<std::size_t>(m.columns()),
                         static_cast<std::size_t>(pane_width(index) / g.cell_width));
    g.rows = std::min(g.rows, static_cast<std::size_t>((pane ? 564 : 580) / g.row_height));
    return g;
}
ui::Grid App::grid() const noexcept
{
    return grid_for(active_tab_);
}
// The last line in view at scroll 0. Normally the screen's end; while the on-screen
// keyboard covers rows, it follows the cursor so the line being typed stays visible.
std::size_t App::view_end() const noexcept
{
    const auto rows = static_cast<std::size_t>(model().rows()), visible = grid().rows;
    std::size_t last = rows;
    if (visible < rows)
        last = std::max(visible, static_cast<std::size_t>(model().cursor().row) + 1);
    const auto end = model().history_size() + std::min(last, rows);
    return end > scroll_ ? end - scroll_ : 0;
}
std::size_t App::max_scroll() const noexcept
{
    // A full-screen program's screen has no history to look at.
    if (model().alternate_screen())
        return 0;
    const auto rows = static_cast<std::size_t>(model().rows()), visible = grid().rows;
    std::size_t last = rows;
    if (visible < rows)
        last = std::max(visible, static_cast<std::size_t>(model().cursor().row) + 1);
    const auto end = model().history_size() + std::min(last, rows);
    return end > visible ? end - visible : 0;
}
bool App::hover(const ui::Rect &rect) const noexcept
{
    return pointer_shown_.alpha(now_) && !(menu_open_ && !settings_open_) &&
           rect.contains(pointer_.x, pointer_.y);
}
// The output position under (x, y), clamped to the visible lines.
ui::TextPos App::text_at(int x, int y) noexcept
{
    auto g = grid();
    if (scroll_ > max_scroll())
        scroll_ = max_scroll();
    auto cell = ui::cell_at(g, x, y);
    const auto history = model().history_size();
    const auto history_first = model().history_first();
    const auto history_end_id = model().history_end();
    const std::size_t view_end = App::view_end();
    std::size_t view_start = view_end > g.rows ? view_end - g.rows : 0;
    std::size_t li = view_start + cell.row;
    if (li >= view_end && view_end > 0)
        li = view_end - 1;
    std::uint64_t serial = li < history ? history_first + li : history_end_id + (li - history);
    return {serial, cell.col};
}
void App::unit(ui::TextPos at, unsigned mode, ui::TextPos &first, ui::TextPos &last) const noexcept
{
    first = last = at;
    const auto cols = static_cast<std::size_t>(model().columns());
    if (mode == 3)
    {
        first.col = 0;
        last.col = cols - 1;
    }
    else if (mode == 2)
    {
        const auto history_end = model().history_end();
        char line_buf[256];
        std::size_t len = 0;
        for (std::size_t c = 0; c < cols && len < sizeof(line_buf); ++c)
        {
            TerminalModel::Cell cell;
            if (at.line < history_end)
                cell = model().history_cell(at.line, c);
            else
                cell = model().cell(static_cast<int>(at.line - history_end), static_cast<int>(c));
            auto cp = cell.codepoints[0];
            line_buf[len++] = (cp >= 0x20 && cp <= 0x7e) ? static_cast<char>(cp) : ' ';
        }
        auto span = ui::word_at({line_buf, len}, at.col);
        first.col = span.first;
        last.col = span.last;
    }
}
void App::mouse(const MouseSample &s, std::uint64_t now) noexcept
{
    now_ = now;
    if (!s.connected)
    {
        mouse_buttons_ = focus_mouse_buttons_ = 0;
        drag_mode_ = 0;
        return;
    }
    if (!s.dx && !s.dy && !s.wheel && !s.buttons && !mouse_buttons_)
        return;
    last_input_ = InputKind::mouse;
    pointer_shown_.mouse(now);
    focus_visible_ = false;
    menu_keyboard_ = false;
    if (s.dx || s.dy)
    {
        pointer_.move(s.dx, s.dy, ui::speeds[speed_]);
        if (divider_drag_ >= 0)
            divider_drag_ = ui::divider_x(ui::split_clamp(pointer_.x - 96 - 40));
        else if (drag_mode_)
            drag();
    }
    if (divider_drag_ >= 0)
    {
        mouse_buttons_ = s.buttons;
        if (!(s.buttons & 1))
        {
            // Resize the terminals once, on release, not on every motion sample.
            move_divider(divider_drag_ - 96 - 40);
            divider_drag_ = -1;
        }
        return;
    }
    if (split_ && !settings_open_ && !password_entry_.active() && !menu_open_ && !search_open_ &&
        (s.buttons & 1) && !(mouse_buttons_ & 1) &&
        std::abs(pointer_.x - ui::divider_x(split_left_)) <= 12 && pointer_.y >= 202 &&
        pointer_.y < 202 + (keyboard_ ? 420 : 626))
    {
        divider_drag_ = ui::divider_x(split_left_);
        mouse_buttons_ = s.buttons;
        return;
    }
    if (focus_mouse_buttons_)
    {
        focus_mouse_buttons_ &= s.buttons;
        mouse_buttons_ = s.buttons;
        return; // Suppress the entire focus gesture through button release.
    }
    const auto pressed = s.buttons & ~mouse_buttons_, released = mouse_buttons_ & ~s.buttons;
    mouse_buttons_ = s.buttons;
    if (((pressed & 7U) || s.wheel) && focus_pointer_pane())
    {
        mouse_buttons_ = s.buttons;
        focus_mouse_buttons_ = s.buttons;
        if (pressed & 7U)
            return;
    }
    if (mouse_to_program(pressed, released, s.wheel))
        return;
    if (pressed & 1)
        left_press(now);
    if (pressed & 2)
    {
        drag_mode_ = 0;
        if (password_entry_.active())
            password_entry_.cancel();
        else if (search_open_)
            close_search();
        else if (settings_open_ && about_open_)
            about_open_ = false;
        else if (settings_open_)
            close_settings(); // right-click = back, like Circle
        else if (menu_open_)
            menu_open_ = false;
        else
        {
            menu_ = ui::menu_rect(pointer_.x, pointer_.y);
            menu_open_ = true;
            menu_keyboard_ = false;
            menu_focus_ = 0;
        }
        log_.line("right x=%d y=%d menu=%d", pointer_.x, pointer_.y, menu_open_ ? 1 : 0);
    }
    if ((pressed & 4) && !settings_open_)
    {
        menu_open_ = false;
        if (search_open_)
            search_key(Key::paste, 0);
        else
            paste();
    }
    if (released & 1)
        drag_mode_ = 0;
    if (s.wheel)
    {
        if (search_open_)
            search_move(s.wheel > 0 ? -1 : 1);
        else
            wheel(s.wheel);
    }
}
// A program that asked for mouse events gets them over its screen. Holding Shift
// keeps the terminal's own selection, as in other terminals.
bool App::mouse_to_program(std::uint32_t pressed, std::uint32_t released, int notches) noexcept
{
    // Shift, or Select text from the actions menu (mouse-only users), selects text
    // instead of clicking in a program that uses the mouse.
    if (!active_connection().native() || settings_open_ || menu_open_ || search_open_ || scroll_ ||
        output_selection_ || model().mouse_reporting() == TerminalModel::MouseReporting::none ||
        (keyboard_mods_ & 0x22))
        return false;
    const auto g = grid();
    const bool inside = g.area().contains(pointer_.x, pointer_.y);
    if (!inside && !(released & 7U))
        return false;
    const auto end = view_end();
    const auto start = end > g.rows ? end - g.rows : 0;
    const auto cell = ui::cell_at(g, pointer_.x, pointer_.y);
    const auto line = start + cell.row;
    const auto history = model().history_size();
    if (line >= history)
        model().mouse_move(static_cast<int>(line - history), static_cast<int>(cell.col));
    // Device bits are left, right, middle; terminal buttons are left, middle, right.
    constexpr unsigned buttons[] = {1, 3, 2};
    for (unsigned i = 0; i < 3; ++i)
    {
        if (pressed & (1U << i))
            model().mouse_button(buttons[i], true);
        if (released & (1U << i))
            model().mouse_button(buttons[i], false);
    }
    for (int i = 0, count = std::min(std::abs(notches), 10); i < count; ++i)
        model().mouse_button(notches > 0 ? 4 : 5, true);
    return inside;
}
void App::left_press(std::uint64_t now) noexcept
{
    const int x = pointer_.x, y = pointer_.y;
    if (!settings_open_ && !password_entry_.active() && !menu_open_)
    {
        if (ui::split_button.contains(x, y))
        {
            toggle_split();
            return;
        }
        if (ui::focus_pane_button.contains(x, y))
        {
            focus_other_pane();
            return;
        }
        if (ui::new_tab_button.contains(x, y))
        {
            new_tab();
            return;
        }
        if (ui::close_tab_button.contains(x, y))
        {
            close_tab();
            return;
        }
        for (unsigned i = 0; i < 4; ++i)
            if (ui::tab_button(i).contains(x, y))
            {
                switch_tab(i);
                return;
            }
    }
    if (search_open_)
    {
        if (ui::search_previous.contains(x, y))
            search_move(-1);
        else if (ui::search_next.contains(x, y))
            search_move(1);
        else if (ui::search_refresh.contains(x, y))
            refresh_search();
        else if (ui::search_close.contains(x, y))
            close_search();
        else if (keyboard_ && ui::osk_shift.contains(x, y))
            shift_ = !shift_;
        else if (keyboard_ && ui::osk_delete.contains(x, y))
            search_key(Key::backspace, 0);
        else if (keyboard_ && ui::osk_run.contains(x, y))
            search_move(1);
        else if (keyboard_ && ui::key_at(x, y) >= 0)
            type_key(static_cast<std::size_t>(ui::key_at(x, y)));
        else if (ui::keyboard_button.contains(x, y))
            keyboard_ = !keyboard_;
        else if (ui::settings_label.contains(x, y))
            open_settings();
        return;
    }
    if (settings_open_)
    {
        settings_click();
        return;
    }
    if (menu_open_)
    {
        menu_open_ = false;
        auto item = ui::menu_item(menu_, x, y);
        log_.line("menu item=%d", static_cast<int>(item));
        if (item != ui::MenuItem::none)
            menu_action(item);
        return;
    }
    if (keyboard_)
    {
        int index = ui::key_at(x, y);
        if (index >= 0)
        {
            type_key(static_cast<std::size_t>(index));
            return;
        }
    }
    switch (ui::main_button_at(x, y, keyboard_))
    {
    case ui::MainButton::settings:
        log_.line("click settings");
        open_settings();
        return;
    case ui::MainButton::keyboard:
        key(Key::toggle_keyboard);
        return;
    case ui::MainButton::reconnect:
        // A click restarts a live guest only when confirmed: it would lose unsaved work.
        if (active_connection().native() && !active_connection().failed())
        {
            if (now_ >= reconnect_armed_until_)
            {
                reconnect_armed_until_ = now_ + 5000000;
                return;
            }
            reconnect_armed_until_ = 0;
            reset_terminal();
            active_connection().reconnect();
            model().feed("[PS5Term] Reconnecting...\r\n");
            return;
        }
        key(Key::reconnect);
        return;
    case ui::MainButton::interrupt:
        key(Key::interrupt);
        return;
    case ui::MainButton::shift:
        shift_ = !shift_;
        return;
    case ui::MainButton::del:
        key(Key::backspace);
        return;
    case ui::MainButton::run:
        submit();
        return;
    case ui::MainButton::none:
        break;
    }
    if (ui::input_area.contains(x, y))
    {
        constexpr auto input_window = text_metrics::input_columns - text_metrics::input_margin;
        std::size_t offset =
            editor().cursor() > input_window ? editor().cursor() - input_window : 0;
        editor().move_to(ui::input_cursor(x, offset, editor().text().size()));
        log_.line("click input x=%d cursor=%zu", x, editor().cursor());
        return;
    }
    if (!grid().area().contains(x, y))
    {
        selection_.clear();
        return;
    }
    auto at = text_at(x, y);
    unsigned count = clicks_.press(now, x, y);
    drag_mode_ = count;
    drag_anchor_ = at;
    if (count == 1)
        selection_.clear(); // a plain click deselects; dragging selects
    else
    {
        ui::TextPos first, last;
        unit(at, count, first, last);
        selection_.begin(first, last);
    }
    log_.line("click output x=%d y=%d line=%llu col=%zu clicks=%u", x, y,
              static_cast<unsigned long long>(at.line), at.col, count);
}
void App::drag() noexcept
{
    auto at = text_at(pointer_.x, pointer_.y);
    if (drag_mode_ == 1 && !selection_.active())
    {
        if (at == drag_anchor_)
            return;
        selection_.begin(drag_anchor_, drag_anchor_);
    }
    ui::TextPos first, last;
    unit(at, drag_mode_, first, last);
    selection_.extend(first, last);
}
void App::wheel(int notches) noexcept
{
    if (settings_open_)
        return;
    menu_open_ = false;
    // A full-screen program has no scrollback here: the wheel moves it instead.
    if (model().alternate_screen())
    {
        const auto arrow = notches > 0 ? TerminalModel::Key::up : TerminalModel::Key::down;
        for (int i = 0, count = std::min(std::abs(notches) * 3, 30); i < count; ++i)
            model().key(arrow);
        return;
    }
    scroll_ = ui::wheel_scroll(scroll_, notches, (keyboard_mods_ & 0x22) != 0, max_scroll());
    log_.line("wheel %d scroll=%zu", notches, scroll_);
}
void App::settings_click() noexcept
{
    if (password_entry_.active())
    {
        password_focus_ = 0;
        const int index = ui::key_at(pointer_.x, pointer_.y);
        if (index >= 0)
        {
            if (static_cast<std::size_t>(index) < ui::character_keys)
            {
                selected_ = static_cast<unsigned>(index);
                const auto keys = shift_ ? upper : lower;
                if (selected_ < keys.size())
                    password_entry_.insert(keys[selected_]);
            }
        }
        else if (ui::osk_shift.contains(pointer_.x, pointer_.y))
            shift_ = !shift_;
        else if (ui::osk_delete.contains(pointer_.x, pointer_.y))
            password_entry_.backspace();
        else if (ui::osk_run.contains(pointer_.x, pointer_.y))
            accept_password();
        else if (ui::help_back.contains(pointer_.x, pointer_.y))
            password_entry_.cancel();
        return;
    }
    if (about_open_)
    {
        if (ui::help_back.contains(pointer_.x, pointer_.y))
            about_open_ = false;
        return;
    }
    auto button = ui::settings_button_at(pointer_.x, pointer_.y);
    log_.line("settings click x=%d y=%d button=%d", pointer_.x, pointer_.y,
              static_cast<int>(button));
    focus_visible_ = false;
    settings_action(button);
}
void App::settings_action(ui::SettingsButton button) noexcept
{
    if (button == ui::SettingsButton::restart_ssh)
        settings_focus_ = 3;
    else if (button == ui::SettingsButton::toggle_ssh)
        settings_focus_ = 0;
    else if (button == ui::SettingsButton::show_password)
        settings_focus_ = 1;
    else if (button == ui::SettingsButton::retry)
        settings_focus_ = 2;
    else if (button == ui::SettingsButton::speed_down)
        settings_focus_ = 4;
    else if (button == ui::SettingsButton::speed_up)
        settings_focus_ = 5;
    else if (button == ui::SettingsButton::toggle_linux)
        settings_focus_ = 6;
    else if (button == ui::SettingsButton::toggle_host_mount)
        settings_focus_ = 7;
    else if (button == ui::SettingsButton::expand_storage)
        settings_focus_ = 8;
    else if (button == ui::SettingsButton::text_size)
        settings_focus_ = 9;
    else if (button == ui::SettingsButton::open_alpine)
        settings_focus_ = 10;
    else if (button == ui::SettingsButton::back)
        settings_focus_ = 11;
    else if (button == ui::SettingsButton::about)
        settings_focus_ = 12;
    switch (button)
    {
    case ui::SettingsButton::restart_ssh:
        if (ssh_enabled_)
            ssh_.retry();
        break;
    case ui::SettingsButton::toggle_ssh:
        toggle_ssh();
        break;
    case ui::SettingsButton::show_password:
        password_visible_ = !password_visible_;
        break;
    case ui::SettingsButton::retry:
#if defined(PS5TERM_OWNED_SSH)
        begin_password();
#else
        if (ssh_enabled_)
            ssh_.retry();
#endif
        break;
    case ui::SettingsButton::toggle_host_mount:
        if (platform::LinuxSubsystem::shared_folder_available() && Connection::linux_available())
        {
            host_mount_error_ = !connection_.set_linux_mounts_whole_console(!host_mount_whole_);
            if (!host_mount_error_)
                host_mount_whole_ = !host_mount_whole_;
        }
        break;
    case ui::SettingsButton::open_alpine:
        switch_tab(0);
        if (Connection::linux_available() && !connection_.storage_busy())
        {
            if (connection_.environment() != Connection::Environment::linux_guest)
            {
                if (!connection_.select_environment(Connection::Environment::linux_guest))
                    break;
                reset_terminal();
            }
            pending_alpine_ = true;
            alpine_ready_at_ = 0;
            close_settings();
        }
        break;
    case ui::SettingsButton::text_size:
        cycle_text_size();
        break;
    case ui::SettingsButton::expand_storage:
        if (connection_.environment() == Connection::Environment::linux_guest)
            storage_result_ = "Stop Linux before expanding storage";
        else if (!connection_.storage_busy() && !confirmed(button))
            break;
        else if (connection_.expand_linux_storage(512))
        {
            storage_requested_ = true;
            storage_result_ = "";
        }
        else
            storage_result_ = "Cannot expand: stop Linux or wait for the current expansion";
        break;
    case ui::SettingsButton::toggle_linux:
        // Stopping ends every Linux shell and SSH linux@ session: ask once more.
        if (connection_.environment() == Connection::Environment::linux_guest && !confirmed(button))
            break;
        switch_tab(0);
        if (connection_.select_environment(connection_.environment() ==
                                                   Connection::Environment::linux_guest
                                               ? Connection::Environment::console
                                               : Connection::Environment::linux_guest))
        {
            // Show the shell that just started (or the PS5 shell after stopping).
            reset_terminal();
            close_settings();
        }
        break;
    case ui::SettingsButton::back:
        close_settings();
        break;
    case ui::SettingsButton::about:
        about_open_ = true;
        break;
    case ui::SettingsButton::speed_down:
        set_speed(speed_ ? speed_ - 1 : 0);
        break;
    case ui::SettingsButton::speed_up:
        set_speed(speed_ + 1);
        break;
    case ui::SettingsButton::none:
        break;
    }
}
void App::menu_action(ui::MenuItem item) noexcept
{
    switch (item)
    {
    case ui::MenuItem::toggle_split:
        toggle_split();
        break;
    case ui::MenuItem::focus_pane:
        focus_other_pane();
        break;
    case ui::MenuItem::divider_left:
    case ui::MenuItem::divider_right:
        move_divider(split_left_ + (item == ui::MenuItem::divider_left ? -80 : 80));
        break;
    case ui::MenuItem::new_tab:
        new_tab();
        break;
    case ui::MenuItem::close_tab:
        close_tab();
        break;
    case ui::MenuItem::next_tab:
        cycle_tab(1);
        break;
    case ui::MenuItem::previous_tab:
        cycle_tab(-1);
        break;
    case ui::MenuItem::search:
        open_search();
        break;
    case ui::MenuItem::select_text:
        keyboard_ = false;
        output_selection_ = true;
        selection_cursor_ = text_at(grid().left, grid().top);
        selection_.begin(selection_cursor_, selection_cursor_);
        break;
    case ui::MenuItem::copy:
        copy();
        break;
    case ui::MenuItem::paste:
        paste();
        break;
    case ui::MenuItem::select_all:
        select_all();
        break;
    case ui::MenuItem::clear:
        clear_screen();
        break;
    case ui::MenuItem::none:
        break;
    }
}
void App::selection_key(Key k) noexcept
{
    const auto first = model().history_first();
    const auto last = model().history_end() + static_cast<std::uint64_t>(model().rows() - 1);
    const auto cols = static_cast<std::size_t>(model().columns());
    selection_cursor_.line = std::clamp(selection_cursor_.line, first, last);
    if (k == Key::escape)
    {
        output_selection_ = false;
        selection_.clear();
        return;
    }
    if (k == Key::enter || k == Key::copy)
    {
        copy();
        output_selection_ = false;
        return;
    }
    if (k == Key::left && selection_cursor_.col)
        --selection_cursor_.col;
    if (k == Key::right && selection_cursor_.col + 1 < cols)
        ++selection_cursor_.col;
    if (k == Key::up && selection_cursor_.line > first)
        --selection_cursor_.line;
    if (k == Key::down && selection_cursor_.line < last)
        ++selection_cursor_.line;
    if (k == Key::pageup)
        selection_cursor_.line -= std::min<std::uint64_t>(12, selection_cursor_.line - first);
    if (k == Key::pagedown)
        selection_cursor_.line += std::min<std::uint64_t>(12, last - selection_cursor_.line);
    if (k == Key::home)
        selection_cursor_.col = 0;
    if (k == Key::end)
        selection_cursor_.col = cols - 1;
    selection_.extend(selection_cursor_, selection_cursor_);
    // Keep the selected end visible without sending arrows to the running program.
    const auto cursor_index = static_cast<std::size_t>(selection_cursor_.line - first);
    const auto end = view_end(), rows = grid().rows;
    if (cursor_index >= end)
        scroll_ -= std::min(scroll_, cursor_index - end + 1);
    else if (cursor_index + rows < end)
        scroll_ += end - rows - cursor_index;
    scroll_ = std::min(scroll_, max_scroll());
}
void App::copy() noexcept
{
    if (!selection_.active())
        return;
    clipboard_.resize(
        ui::selection_text(model(), selection_, clipboard_.buffer(), ui::Clipboard::capacity));
    log_.line("copy %zu bytes", clipboard_.text().size());
}
void App::paste() noexcept
{
    if (settings_open_ || clipboard_.text().empty())
        return;
    std::size_t size = 0;
    if (active_connection().native())
    {
        // A program gets line structure intact: newline becomes Enter and tabs stay.
        // Other control bytes go, so pasted text can never end bracketed paste early.
        for (char ch : clipboard_.text())
        {
            const auto byte = static_cast<unsigned char>(ch);
            if (ch == '\n')
                ch = '\r';
            else if ((byte < 32 && ch != '\t' && ch != '\r') || byte == 127)
                continue;
            paste_buffer_[size++] = ch;
        }
        scroll_ = 0;
        model().paste({paste_buffer_.data(), size});
    }
    else
    {
        char text[Editor::limit];
        size = ui::paste_text(clipboard_.text(), text, sizeof(text));
        editor().insert({text, size});
    }
    log_.line("paste %zu bytes", size);
}
void App::select_all() noexcept
{
    const auto first_id = model().history_first();
    const auto model_rows = static_cast<std::size_t>(model().rows());
    const auto cols = static_cast<std::size_t>(model().columns());
    const auto last_id = model().history_end() + model_rows - 1;
    selection_.begin({first_id, 0}, {first_id, 0});
    selection_.extend({last_id, cols - 1}, {last_id, cols - 1});
    log_.line("select all");
}
#if defined(PS5TERM_OWNED_SSH)
int AppGuest::open(unsigned columns, unsigned rows, std::string_view term, std::string_view command,
                   bool terminal) noexcept
{
    if (!Connection::linux_available())
        return -1;
    auto &connection = app_.connection_;
    if (connection.environment() != Connection::Environment::linux_guest)
    {
        if (!connection.select_environment(Connection::Environment::linux_guest))
            return -1;
        app_.switch_tab(0);
        app_.reset_terminal();
        return 0;
    }
    if (connection.failed())
    {
        app_.switch_tab(0);
        app_.reset_terminal();
        connection.reconnect();
        return 0;
    }
    auto &agent = connection.guest_agent();
    if (agent.failed())
        return ssh::guest_link_failed; // Fail fast instead of a silent two-minute wait.
    return agent.alive() ? agent.open(columns, rows, term, command, terminal) : 0;
}
std::size_t AppGuest::write(int handle, std::string_view bytes) noexcept
{
    return app_.connection_.guest_agent().write(handle, bytes);
}
std::size_t AppGuest::read(int handle, char *bytes, std::size_t capacity) noexcept
{
    return app_.connection_.guest_agent().read(handle, bytes, capacity);
}
void AppGuest::resize(int handle, unsigned columns, unsigned rows) noexcept
{
    app_.connection_.guest_agent().resize(handle, columns, rows);
}
void AppGuest::finish_input(int handle) noexcept
{
    app_.connection_.guest_agent().finish_input(handle);
}
void AppGuest::close(int handle) noexcept
{
    app_.connection_.guest_agent().close(handle);
}
int AppGuest::poll(int handle, int *status) noexcept
{
    const auto state = app_.connection_.guest_agent().state(handle, status);
    return state == platform::GuestAgent::State::exited ? 1
           : state == platform::GuestAgent::State::gone ? -1
                                                        : 0;
}
#endif
// A new session must not inherit the last one's modes, colors, history or typing.
void App::reset_terminal() noexcept
{
    editor().clear();
    keyboard_ = false;
    shift_ = false;
    osk_modifiers_ = 0;
    clear_screen();
    model().feed("\033c");
    model().clear_history();
    std::array<char, 4096> discarded{};
    while (model().take_output(discarded))
    {
    }
}
// Ctrl+key as the program expects: a C0 control byte, in order with other keys.
bool App::send_control(char character) noexcept
{
    char ch = character;
    if (ch >= 'A' && ch <= 'Z')
        ch = static_cast<char>(ch - 'A' + 'a');
    char code = 0;
    if (ch >= 'a' && ch <= 'z')
        code = static_cast<char>(ch - 'a' + 1);
    else
        switch (ch)
        {
        case '@':
        case '2':
        case ' ':
            code = 0;
            break;
        case '[':
        case '3':
            code = 27;
            break;
        case '\\':
        case '4':
            code = 28;
            break;
        case ']':
        case '5':
            code = 29;
            break;
        case '^':
        case '6':
            code = 30;
            break;
        case '_':
        case '-':
        case '/':
        case '7':
            code = 31;
            break;
        case '?':
        case '8':
            code = 127;
            break;
        default:
            return false;
        }
    scroll_ = 0;
    return model().raw({&code, 1});
}
void App::clear_screen() noexcept
{
    close_search();
    output_selection_ = false;
    model().feed("\033[2J\033[H");
    selection_.clear();
    drag_mode_ = 0;
    scroll_ = 0;
}
void App::open_settings() noexcept
{
    close_search();
    output_selection_ = false;
    settings_open_ = true;
    about_open_ = false;
    settings_focus_ = 0;
    host_mount_whole_ = active_connection().linux_mounts_whole_console();
    host_mount_error_ = false;
    focus_visible_ = true;
    password_visible_ = false;
    password_entry_.cancel();
    menu_open_ = false;
}
void App::close_settings() noexcept
{
    settings_open_ = false;
    about_open_ = false;
    password_visible_ = false;
    password_entry_.cancel();
}
void App::set_speed(std::size_t index) noexcept
{
    if (index >= ui::speeds.size())
        index = ui::speeds.size() - 1;
    if (index == speed_)
        return;
    speed_ = index;
    bool saved = ui::save_speed(data_directory, speed_file, speed_);
    log_.line("speed %u%% saved=%d", ui::speeds[speed_], saved ? 1 : 0);
}
void App::move_selection(std::uint32_t p) noexcept
{
    const auto count = static_cast<unsigned>(ui::keyboard_keys);
    if (p & 0x0010)
        selected_ = (selected_ + count - 12) % count;
    if (p & 0x0040)
        selected_ = (selected_ + 12) % count;
    if (p & 0x0080)
        selected_ = (selected_ + count - 1) % count;
    if (p & 0x0020)
        selected_ = (selected_ + 1) % count;
}
// A terminal-key press: modifiers go straight to a program; the line editor and
// scrollback handle it through the normal key path otherwise.
// The on-screen Shift applies to terminal keys as well (Shift+Tab, Shift+arrows).
void App::terminal_key(TerminalModel::Key terminal, Key fallback) noexcept
{
    if (shift_ && fallback == Key::tab)
        fallback = Key::backtab;
    if (search_open_)
    {
        key(fallback);
        return;
    }
    const unsigned modifiers = osk_modifiers_ | (shift_ ? TerminalModel::shift : 0U);
    if (active_connection().native() && modifiers)
        model().key(terminal, modifiers);
    else
        key(fallback);
    osk_modifiers_ = 0;
}
void App::type_key(std::size_t index) noexcept
{
    selected_ = static_cast<unsigned>(index);
    log_.line("osk key=%zu", index);
    auto keys = shift_ ? upper : lower;
    if (index < keys.size())
    {
        const char character = keys[index];
        if (search_open_)
        {
            key(Key::character, character);
            return;
        }
        if (osk_modifiers_ & TerminalModel::control)
            key(Key::control, character);
        else if (osk_modifiers_ & TerminalModel::alt)
            key(Key::alt, character);
        else if (active_connection().native())
            model().text(static_cast<unsigned char>(character));
        else
            editor().insert({&character, 1});
        osk_modifiers_ = 0;
        return;
    }
    using T = TerminalModel;
    switch (index)
    {
    case 48:
        terminal_key(T::Key::escape, Key::escape);
        break;
    case 49:
        terminal_key(T::Key::tab, Key::tab);
        break;
    case 50:
        osk_modifiers_ ^= T::control;
        break;
    case 51:
        osk_modifiers_ ^= T::alt;
        break;
    case 52:
        terminal_key(T::Key::left, Key::left);
        break;
    case 53:
        terminal_key(T::Key::down, Key::down);
        break;
    case 54:
        terminal_key(T::Key::up, Key::up);
        break;
    case 55:
        terminal_key(T::Key::right, Key::right);
        break;
    case 56:
        terminal_key(T::Key::page_up, Key::pageup);
        break;
    case 57:
        terminal_key(T::Key::page_down, Key::pagedown);
        break;
    case 58:
        terminal_key(T::Key::home, Key::home);
        break;
    case 59:
        terminal_key(T::Key::end, Key::end);
        break;
    case 70:
        terminal_key(T::Key::erase, Key::del);
        break;
    case 71:
        terminal_key(T::Key::insert, Key::insert);
        break;
    default:
        if (index >= 60 && index < 70) // F1..F10, for programs such as htop and mc
        {
            if (active_connection().native() && !search_open_)
                model().function_key(static_cast<unsigned>(index - 59),
                                     osk_modifiers_ | (shift_ ? T::shift : 0U));
            else
                tab_notice_ = "F-keys go to Linux programs; the built-in shell has none";
            osk_modifiers_ = 0;
        }
        break;
    }
}
void App::draw_menu(ps5::demo::Canvas &c) noexcept
{
    const auto &m = menu_;
    c.rectangle(to_unsigned(m.x), to_unsigned(m.y), to_unsigned(m.w), to_unsigned(m.h), edge);
    c.rectangle(to_unsigned(m.x + 2), to_unsigned(m.y + 2), to_unsigned(m.w - 4),
                to_unsigned(m.h - 4), surface);
    auto hovered = menu_keyboard_               ? static_cast<ui::MenuItem>(menu_focus_)
                   : pointer_shown_.alpha(now_) ? ui::menu_item(m, pointer_.x, pointer_.y)
                                                : ui::MenuItem::none;
    for (std::size_t i = 0; i < ui::menu_items; ++i)
    {
        auto item = static_cast<ui::MenuItem>(i);
        const bool enabled = menu_enabled(item);
        unsigned y = to_unsigned(m.y + 4 + static_cast<int>(i) * ui::menu_item_height);
        if (item == hovered && (enabled || menu_keyboard_))
            c.rectangle(to_unsigned(m.x + 4), y, to_unsigned(m.w - 8), ui::menu_item_height,
                        hover_bg);
        c.text(to_unsigned(m.x + 20), y + 14, ui::menu_label(item), 2, enabled ? text : edge);
    }
}
bool App::focused(const ui::Rect &r) const noexcept
{
    if (!settings_open_ || !focus_visible_)
        return false;
    const auto same = [&r](const ui::Rect &other) { return r.x == other.x && r.y == other.y; };
    if (about_open_)
        return same(ui::help_back);
    if (password_entry_.active())
        return (password_focus_ == 1 && same(ui::osk_shift)) ||
               (password_focus_ == 2 && same(ui::osk_delete)) ||
               (password_focus_ == 3 && same(ui::osk_run)) ||
               (password_focus_ == 4 && same(ui::help_back));
    return (settings_focus_ == 0 && (same(ui::ssh_switch) || same(ui::help_toggle))) ||
           (settings_focus_ == 1 && same(ui::help_password)) ||
           (settings_focus_ == 2 && same(ui::help_retry)) ||
           (settings_focus_ == 3 && same(ui::ssh_retry)) ||
           (settings_focus_ == 4 && same(ui::speed_down_rect)) ||
           (settings_focus_ == 5 && same(ui::speed_up_rect)) ||
           (settings_focus_ == 6 && same(ui::linux_switch)) ||
           (settings_focus_ == 7 && same(ui::host_mount_switch)) ||
           (settings_focus_ == 8 && same(ui::storage_expand)) ||
           (settings_focus_ == 9 && same(ui::text_size_button)) ||
           (settings_focus_ == 10 && same(ui::alpine_button)) ||
           (settings_focus_ == 11 && same(ui::help_back)) ||
           (settings_focus_ == 12 && same(ui::help_about));
}
void App::label_button(ps5::demo::Canvas &c, const ui::Rect &r, std::string_view label,
                       unsigned scale, ps5::demo::Color color) noexcept
{
    if (hover(r) || focused(r))
        c.rectangle(to_unsigned(r.x), to_unsigned(r.y), to_unsigned(r.w), to_unsigned(r.h),
                    hover_bg);
    if (focused(r))
        c.rectangle(to_unsigned(r.x), to_unsigned(r.y), 3, to_unsigned(r.h), cyan);
    c.text(to_unsigned(r.x + 8), to_unsigned(r.y + 8), label, scale,
           (hover(r) || focused(r)) ? text : color);
}
void App::draw_pointer(ps5::demo::Canvas &c) noexcept
{
    unsigned alpha = pointer_shown_.alpha(now_);
    if (!alpha)
    {
        pointer_shown_.shown = false;
        return;
    }
    // Arrow, 2 px per cell: '#' outline, 'o' fill, hotspot at the tip.
    // Plain char rows (no pointers, so it stays in .rodata).
    static constexpr char arrow[17][12] = {
        "#",        "##",       "#o#",       "#oo#",       "#ooo#",       "#oooo#",
        "#ooooo#",  "#oooooo#", "#ooooooo#", "#oooooooo#", "#ooooo#####", "#oo#oo#",
        "#o# #oo#", "##  #oo#", "#    #oo#", "     #oo#",  "      ##"};
    const auto outline = blend(bg, rgb(0, 0, 0), alpha),
               fill = blend(bg, rgb(255, 255, 255), alpha);
    const auto x = to_unsigned(pointer_.x), y = to_unsigned(pointer_.y);
    for (unsigned row = 0; row < sizeof(arrow) / sizeof(arrow[0]); ++row)
        for (unsigned col = 0; col < 12 && arrow[row][col]; ++col)
            if (arrow[row][col] != ' ')
                c.rectangle(x + col * 2, y + row * 2, 2, 2,
                            arrow[row][col] == '#' ? outline : fill);
}
} // namespace term

namespace term
{
void App::begin_password() noexcept
{
#if defined(PS5TERM_OWNED_SSH)
    password_visible_ = false;
    password_result_ = "";
    password_entry_.begin();
    selected_ = 0;
    shift_ = false;
    password_focus_ = 0;
#endif
}
void App::accept_password() noexcept
{
    if (!password_entry_.advance())
        return;
#if defined(PS5TERM_OWNED_SSH)
    if (ssh_.set_password(password_entry_.value()))
    {
        password_entry_.cancel();
        password_result_ = "Password saved. SSH clients disconnected.";
        return;
    }
#endif
    password_entry_.failed();
}
void App::password_key(Key key, char ch) noexcept
{
    if (key == Key::tab || key == Key::backtab)
    {
        password_focus_ = (password_focus_ + (key == Key::backtab ? 4 : 1)) % 5;
        focus_visible_ = true;
        return;
    }
    if (password_focus_ && (key == Key::enter || (key == Key::character && ch == ' ')))
    {
        if (password_focus_ == 1)
            shift_ = !shift_;
        else if (password_focus_ == 2)
            password_entry_.backspace();
        else if (password_focus_ == 3)
            accept_password();
        else
            password_entry_.cancel();
        return;
    }
    switch (key)
    {
    case Key::character:
        password_focus_ = 0;
        password_entry_.insert(ch);
        break;
    case Key::backspace:
        password_entry_.backspace();
        break;
    case Key::del:
        password_entry_.erase();
        break;
    case Key::left:
        password_entry_.left();
        break;
    case Key::right:
        password_entry_.right();
        break;
    case Key::home:
        password_entry_.home();
        break;
    case Key::end:
        password_entry_.end();
        break;
    case Key::enter:
        accept_password();
        break;
    case Key::escape:
        password_entry_.cancel();
        break;
    case Key::settings:
        close_settings();
        break;
    default:
        break; // Never copy, paste, reveal or send a secret to the terminal.
    }
}
void App::password_controller(std::uint32_t p) noexcept
{
    if (p & 0x0008)
    {
        close_settings();
        return;
    }
    if (p & 0x2000)
    {
        password_entry_.cancel();
        return;
    }
    if (p & 0x0004)
        shift_ = !shift_;
    if (p & 0x8000)
        password_entry_.backspace();
    if (p & 0x0010)
        selected_ = (selected_ + 36) % 48;
    if (p & 0x0040)
        selected_ = (selected_ + 12) % 48;
    if (p & 0x0080)
        selected_ = (selected_ + 47) % 48;
    if (p & 0x0020)
        selected_ = (selected_ + 1) % 48;
    const auto keys = shift_ ? upper : lower;
    if ((p & 0x4000) && selected_ < keys.size())
        password_entry_.insert(keys[selected_]);
    if (p & 0x0200)
        accept_password();
}
void App::draw_password(ps5::demo::Canvas &c) noexcept
{
    c.clear(bg);
    c.text(84, 70, "PS5Term / Change SSH password", 3, text);
    c.text(96, 210, password_entry_.confirming() ? "CONFIRM PASSWORD" : "NEW PASSWORD", 2, cyan);
    c.text(96, 270, password_entry_.message(), 2, muted);
    c.text(96, 330, "Saving disconnects SSH clients. The host key and public keys stay the same.",
           2, muted);
    c.rectangle(84, 410, 1752, 70, surface);
    if (password_focus_ == 0 && focus_visible_)
        c.rectangle(84, 410, 3, 70, cyan);
    std::array<char, 64> mask{};
    mask.fill('*');
    c.text(108, 434, {mask.data(), password_entry_.size()}, 2, text);
    c.rectangle(108 + static_cast<unsigned>(password_entry_.cursor()) * text_metrics::cell_width,
                430, 2, 30, cyan);
    c.text(96, 540,
           "Type to edit. Tab selects buttons; Enter continues; Esc cancels. Or click the keys.", 2,
           muted);
    label_button(c, ui::osk_shift, "Shift", 1, shift_ ? text : muted);
    label_button(c, ui::osk_delete, "Delete", 1, muted);
    label_button(c, ui::osk_run, password_entry_.confirming() ? "Save" : "Next", 1, cyan);
    const auto keys = shift_ ? upper : lower;
    for (unsigned i = 0; i < 48; ++i)
    {
        const unsigned x = to_unsigned(ui::key_rect(i).x), y = to_unsigned(ui::key_rect(i).y);
        c.rectangle(x, y, 135, 30,
                    i == selected_           ? cyan
                    : hover(ui::key_rect(i)) ? hover_bg
                                             : surface);
        if (i < keys.size())
            c.text(x + 53, y + 7, keys[i] == ' ' ? "_" : slice(keys, i, 1), 2,
                   i == selected_ ? bg : text);
    }
    c.text(84, 900, "Mouse: click keys and buttons. Keyboard: Tab / Shift+Tab, Enter / Space.", 2,
           muted);
    c.text(84, 944, "Controller: D-pad select, Cross type, R3 shift, Square delete, R2 next/save.",
           1, muted);
    label_button(c, ui::help_back, "Cancel (Esc)", 2, muted);
}
} // namespace term

namespace term
{
void App::open_search() noexcept
{
    output_selection_ = menu_open_ = false;
    selection_.clear();
    drag_mode_ = 0;
    search_open_ = true;
    keyboard_ = false;
    osk_modifiers_ = 0;
    refresh_search();
}
void App::close_search() noexcept
{
    if (search_open_)
        keyboard_ = false;
    search_open_ = search_selected_ = false;
}
void App::refresh_search() noexcept
{
    search_index_ = 0;
    search_selected_ = false;
    search_.start(model(), search_editor_.text());
}
void App::reveal_search() noexcept
{
    if (!search_selected_ || search_index_ >= search_.count())
        return;
    const auto match = search_.match(search_index_);
    const auto first = model().history_first(), end = model().history_end();
    if (match.line < first || match.line >= end + static_cast<std::uint64_t>(model().rows()))
    {
        refresh_search();
        return;
    }
    const auto total = model().history_size() + static_cast<std::size_t>(model().rows());
    const auto index = static_cast<std::size_t>(match.line - first);
    scroll_ = std::min(max_scroll(), total > index + grid().rows ? total - index - grid().rows : 0);
}
void App::search_move(int direction) noexcept
{
    if (!search_.count())
        return;
    keyboard_ = false;
    search_index_ = direction < 0 ? (search_index_ + search_.count() - 1) % search_.count()
                                  : (search_index_ + 1) % search_.count();
    search_selected_ = true;
    reveal_search();
}
void App::search_key(Key k, char ch) noexcept
{
    if (k == Key::escape || k == Key::interrupt)
    {
        close_search();
        return;
    }
    if (k == Key::toggle_keyboard)
    {
        keyboard_ = !keyboard_;
        return;
    }
    if (k == Key::enter || k == Key::down)
    {
        search_move(1);
        return;
    }
    if (k == Key::backtab || k == Key::up)
    {
        search_move(-1);
        return;
    }
    if (k == Key::reconnect)
    {
        refresh_search();
        return;
    }
    const auto before = search_editor_.text();
    // Editor mutations use the query buffer only. Never call model().key/raw here.
    if (k == Key::character && ch >= 32 && ch <= 126 && before.size() < TerminalSearch::query_limit)
        search_editor_.insert({&ch, 1});
    else if (k == Key::paste)
    {
        auto value = clipboard_.text();
        if (value.size() <= TerminalSearch::query_limit - before.size())
            search_editor_.insert(value);
    }
    else if (k == Key::backspace)
        search_editor_.backspace();
    else if (k == Key::del)
        search_editor_.erase();
    else if (k == Key::left)
    {
        search_editor_.left();
        return;
    }
    else if (k == Key::right)
    {
        search_editor_.right();
        return;
    }
    else if (k == Key::home)
    {
        search_editor_.home();
        return;
    }
    else if (k == Key::end)
    {
        search_editor_.end();
        return;
    }
    else if (k == Key::control && ch == 'a')
    {
        search_editor_.home();
        return;
    }
    else if (k == Key::control && ch == 'e')
    {
        search_editor_.end();
        return;
    }
    else if (k == Key::clear || (k == Key::control && ch == 'u'))
        search_editor_.clear();
    else
        return;
    refresh_search();
}
void App::draw_search(ps5::demo::Canvas &c) noexcept
{
    c.rectangle(64, 850, 1792, 76, surface);
    c.text(84, 862, "Find (literal):", 1, cyan);
    const auto query = search_editor_.text();
    const auto cursor = search_editor_.cursor();
    // The query is validated UTF-8. Render codepoints rather than one '?' per
    // encoded byte, and keep the insertion point visible for long queries.
    std::size_t begin = cursor > 90 ? cursor - 90 : 0;
    while (begin && (static_cast<unsigned char>(query[begin]) & 0xc0) == 0x80)
        --begin;
    unsigned x = 84, caret = x;
    for (std::size_t i = begin; i < query.size() && x < 1014;)
    {
        if (i == cursor)
            caret = x;
        const auto byte = static_cast<unsigned char>(query[i++]);
        unsigned cp = byte, remaining = 0;
        if (byte >= 0xc0)
        {
            remaining = byte < 0xe0 ? 1 : byte < 0xf0 ? 2 : 3;
            cp = byte & ((1u << (6 - remaining)) - 1);
        }
        while (remaining--)
            cp = (cp << 6) | (static_cast<unsigned char>(query[i++]) & 63);
        c.glyph(x, 886, cp, text, 2);
        x += text_metrics::advance(2);
        if (i == cursor)
            caret = x;
    }
    c.rectangle(caret, 884, 2, 20, cyan);
    label_button(c, ui::search_previous, "Previous", 1, cyan);
    label_button(c, ui::search_next, "Next", 1, cyan);
    label_button(c, ui::search_refresh, "Refresh", 1, cyan);
    label_button(c, ui::search_close, "Close (Esc)", 1, cyan);
    c.rectangle(64, 936, 1792, 40, bg);
    char status[256];
    std::snprintf(status, sizeof(status),
                  "%zu / %zu%s%s  | Enter/Down: next; Up: previous; F5: refresh; Triangle: "
                  "keyboard; L1/R1: matches; Circle: close",
                  search_selected_ ? search_index_ + 1 : 0, search_.count(),
                  search_.truncated() ? "+" : "", search_.busy() ? " (searching)" : "");
    c.text(84, 951, status, 1, cyan);
}
} // namespace term

namespace term
{
void App::toggle_split() noexcept
{
    if (split_)
    {
        split_ = false;
        sync_terminal_size();
        tab_notice_ = "Full-width view; both shells keep running";
        return;
    }
    const auto first = active_tab_;
    unsigned second = first;
    for (unsigned step = 1; step < 4; ++step)
    {
        const auto i = (first + step) % 4;
        if (!i || tabs_[i - 1])
        {
            second = i;
            break;
        }
    }
    if (second == first)
    {
        if (!new_tab())
            return;
        second = active_tab_;
    }
    panes_ = {first, second};
    split_ = true;
    switch_tab(second);
    sync_terminal_size();
    tab_notice_ = "Input goes to the highlighted pane; focus with Ctrl+Shift+O or L3 Actions";
}
void App::focus_other_pane() noexcept
{
    if (split_)
        switch_tab(panes_[active_tab_ == panes_[0] ? 1 : 0]);
    else
        tab_notice_ = "Split first to show two terminal panes";
}
int App::pane_width(unsigned index) const noexcept
{
    if (!split_ || (index != panes_[0] && index != panes_[1]))
        return 1720;
    return index == panes_[0] ? split_left_ : ui::split_total - split_left_;
}
void App::move_divider(int left) noexcept
{
    if (!split_)
    {
        tab_notice_ = "Split first to show two terminal panes";
        return;
    }
    left = ui::split_clamp(left);
    if (left == split_left_)
        return;
    split_left_ = left;
    sync_terminal_size(); // both panes' programs see their new width
    tab_notice_ = "Divider moved; drag it, Ctrl+Shift+Left/Right, or L3 Actions";
}
bool App::focus_pointer_pane() noexcept
{
    if (!split_ || settings_open_ || password_entry_.active() || menu_open_ || search_open_ ||
        drag_mode_)
        return false;
    const auto other = panes_[active_tab_ == panes_[0] ? 1 : 0];
    if (!grid_for(other).area().contains(pointer_.x, pointer_.y))
        return false;
    switch_tab(other);
    // Consume the focus click; it must not select text or reach a running program.
    mouse_buttons_ = 0;
    return true;
}
bool App::new_tab() noexcept
{
    for (unsigned i = 0; i < tabs_.size(); ++i)
    {
        if (tabs_[i])
            continue;
        auto *peer = active_connection().environment() == Connection::Environment::linux_guest
                         ? &connection_.guest_agent()
                         : nullptr;
        if (peer && !peer->alive())
        {
            tab_notice_ = "Linux is starting or stopped; retry New tab when ready";
            return false;
        }
        auto tab =
            std::unique_ptr<Tab>(new (std::nothrow) Tab(model().rows(), model().columns(), peer));
        if (!tab || !tab->model.healthy())
        {
            tab_notice_ = "Cannot allocate a terminal tab";
            return false;
        }
        tab->model.set_default_colors({212, 224, 237}, {0, 0, 0});
        tabs_[i] = std::move(tab);
        switch_tab(i + 1);
        tab_notice_ = "New tab uses the current shell type; Settings controls the shared Linux VM";
        return true;
    }
    tab_notice_ = "Four tabs maximum. Close a tab before opening another.";
    return false;
}
void App::switch_tab(unsigned index) noexcept
{
    if (index > tabs_.size() || (index && !tabs_[index - 1]) || index == active_tab_)
        return;
    if (active_tab_)
        tabs_[active_tab_ - 1]->scroll = scroll_;
    else
        primary_scroll_ = scroll_;
    for (unsigned i = 0; i < 3; ++i)
        if (mouse_buttons_ & (1u << i))
            model().mouse_button(i == 0 ? 1 : i == 1 ? 3 : 2, false);
    close_search();
    selection_.clear();
    output_selection_ = menu_open_ = keyboard_ = false;
    drag_mode_ = mouse_buttons_ = osk_modifiers_ = 0;
    reconnect_armed_until_ = 0;
    if (split_ && index != panes_[0] && index != panes_[1])
        panes_[active_tab_ == panes_[1] ? 1 : 0] = index;
    active_tab_ = index;
    scroll_ = index ? tabs_[index - 1]->scroll : primary_scroll_;
    seen_history_end_ = model().history_end();
    was_alternate_ = model().alternate_screen();
    sync_terminal_size();
}
// Settings focus follows the screen from top to bottom (indexes are the actions[]
// order in key()) and skips controls that are not shown.
// A second press within five seconds confirms an action that ends work.
std::string_view App::pick(std::string_view keyboard, std::string_view mouse,
                           std::string_view controller) const noexcept
{
    return last_input_ == InputKind::keyboard ? keyboard
           : last_input_ == InputKind::mouse  ? mouse
                                              : controller;
}
bool App::confirmed(ui::SettingsButton button) noexcept
{
    if (armed_button_ == button && now_ < armed_until_)
    {
        armed_button_ = ui::SettingsButton::none;
        armed_until_ = 0;
        return true;
    }
    armed_button_ = button;
    armed_until_ = now_ + 5000000;
    return false;
}
void App::move_settings_focus(int direction) noexcept
{
    static constexpr unsigned order[] = {0, 3, 6, 7, 10, 8, 9, 4, 5, 1, 2, 11, 12};
    constexpr unsigned count = sizeof order / sizeof *order;
    unsigned at = 0;
    while (at < count && order[at] != settings_focus_)
        ++at;
    // Only the /mnt/ps5 choice is hidden when unavailable (see draw_settings).
    const bool shares =
        Connection::linux_available() && platform::LinuxSubsystem::shared_folder_available();
    for (unsigned step = 0; step < count; ++step)
    {
        at = (at + (direction < 0 ? count - 1 : 1U)) % count;
        const unsigned candidate = order[at];
        if (candidate != 7 || shares)
        {
            settings_focus_ = candidate;
            return;
        }
    }
}
bool App::menu_enabled(ui::MenuItem item) const noexcept
{
    return item == ui::MenuItem::copy    ? selection_.active()
           : item == ui::MenuItem::paste ? !clipboard_.text().empty()
           : item == ui::MenuItem::divider_left || item == ui::MenuItem::divider_right ? split_
                                                                                       : true;
}
// Keyboard and controller focus skips actions that cannot run now.
void App::move_menu_focus(int direction) noexcept
{
    for (std::size_t step = 0; step < ui::menu_items; ++step)
    {
        constexpr auto count = static_cast<unsigned>(ui::menu_items);
        menu_focus_ = (menu_focus_ + (direction < 0 ? count - 1 : 1U)) % count;
        if (menu_enabled(static_cast<ui::MenuItem>(menu_focus_)))
            return;
    }
}
bool App::program_owns_function_keys() const noexcept
{
    return !settings_open_ && !menu_open_ && !search_open_ && !output_selection_ &&
           active_connection().native() && active_connection().connected();
}
unsigned App::open_tabs() const noexcept
{
    return 1U + static_cast<unsigned>(std::count_if(tabs_.begin(), tabs_.end(),
                                                    [](const auto &tab) { return bool(tab); }));
}
void App::cycle_tab(int direction) noexcept
{
    unsigned index = active_tab_;
    for (unsigned i = 0; i < 4; ++i)
    {
        index = (index + (direction < 0 ? 3 : 1)) % 4;
        if (!index || tabs_[index - 1])
        {
            switch_tab(index);
            return;
        }
    }
}
void App::close_tab() noexcept
{
    if (!active_tab_)
    {
        tab_notice_ = "Primary tab owns the subsystem; use Settings to stop Linux";
        return;
    }
    const auto old = active_tab_;
    const unsigned survivor = split_ ? panes_[active_tab_ == panes_[0] ? 1 : 0] : 0;
    split_ = false;
    switch_tab(survivor);
    tabs_[old - 1].reset();
    sync_terminal_size();
    tab_notice_ = "Tab closed; other shells keep running";
}
void App::draw_tabs(ps5::demo::Canvas &c) noexcept
{
    label_button(c, ui::split_button,
                 split_ ? pick("Unsplit (Ctrl+Shift+E)", "Unsplit", "Unsplit (L3 menu)")
                        : pick("Split (Ctrl+Shift+E)", "Split", "Split (L3 menu)"),
                 1, cyan);
    label_button(c, ui::focus_pane_button,
                 pick("Focus other (Ctrl+Shift+O)", "Focus other", "Focus other (L3 menu)"), 1,
                 split_ ? cyan : muted);
    label_button(c, ui::new_tab_button,
                 pick("New tab (Ctrl+Shift+T)", "New tab", "New tab (L3 menu)"), 1, cyan);
    label_button(c, ui::close_tab_button, pick("Close (Ctrl+Shift+W)", "Close", "Close (L3 menu)"),
                 1, active_tab_ ? cyan : muted);
    for (unsigned i = 0; i < 4; ++i)
    {
        if (i && !tabs_[i - 1])
            continue;
        auto &connection = i ? tabs_[i - 1]->connection : connection_;
        char title[24];
        std::snprintf(title, sizeof title, "%u %s%s", i + 1,
                      connection.environment() == Connection::Environment::linux_guest ? "Linux"
                                                                                       : "console",
                      connection.failed() ? " !" : "");
        auto rect = ui::tab_button(i);
        if (i == active_tab_)
            c.rectangle(rect.x, rect.y, rect.w, rect.h, edge);
        label_button(c, rect, title, 1, i == active_tab_ ? cyan : muted);
    }
    c.text(700, 116, tab_notice_, 1, muted);
}
} // namespace term
