// SPDX-License-Identifier: GPL-3.0-or-later
// App-level VT input modes, paste, replies and controller resize.
#include "app.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
namespace term
{
struct AppTest
{
    struct Peer
    {
        std::string input, output;
        unsigned rows = 0, cols = 0;
        bool exited = false;
    };
    static platform::LaunchResult start(void *, const platform::LaunchSpec &) noexcept
    {
        return {{1, 1}, 0};
    }
    static platform::ChildState poll(void *p, platform::ChildId) noexcept
    {
        return {static_cast<Peer *>(p)->exited ? platform::ChildPhase::exited
                                               : platform::ChildPhase::running,
                0, 0};
    }
    static platform::StopResult stop(void *, platform::ChildId) noexcept
    {
        return {true, 0};
    }
    static platform::IoResult write(void *p, platform::ChildId, const char *b,
                                    std::size_t n) noexcept
    {
        auto &peer = *static_cast<Peer *>(p);
        auto accepted = std::min(n, std::size_t(2));
        peer.input.append(b, accepted);
        return {accepted, platform::IoState::ok, 0};
    }
    static platform::IoResult read(void *p, platform::ChildId, char *b, std::size_t n) noexcept
    {
        auto &peer = *static_cast<Peer *>(p);
        auto count = std::min(n, peer.output.size());
        std::copy_n(peer.output.data(), count, b);
        peer.output.erase(0, count);
        return {count,
                count         ? platform::IoState::ok
                : peer.exited ? platform::IoState::eof
                              : platform::IoState::would_block,
                0};
    }
    static bool resize(void *p, platform::ChildId, unsigned cols, unsigned rows) noexcept
    {
        auto &peer = *static_cast<Peer *>(p);
        peer.rows = rows;
        peer.cols = cols;
        return true;
    }
    static bool signal(void *, platform::ChildId, platform::ChildSignal) noexcept
    {
        return true;
    }
    static void run()
    {
        Peer peer;
        App app({&peer, start, poll, stop, write, read, resize, signal});
        auto pump = [&]
        {
            for (int i = 0; i < 20; ++i)
                app.connection_.tick(app.model_, i);
        };
        pump();
        assert(app.connection_.native());
        app.model_.feed("\033[?1h\033[?2004h");
        app.key(Key::up);
        pump();
        assert(peer.input == "\033OA");
        peer.input.clear();
        app.key(Key::tab);
        app.key(Key::backtab);
        pump();
        assert(peer.input == "\t\033[Z");
        peer.input.clear();
        const char pasted[] = "paste me";
        std::memcpy(app.clipboard_.buffer(), pasted, sizeof(pasted) - 1);
        app.clipboard_.resize(sizeof(pasted) - 1);
        app.key(Key::paste);
        pump();
        assert(peer.input == "\033[200~paste me\033[201~");
        peer.input.clear();
        peer.output = "\033[H\033[6n";
        pump();
        assert(peer.input == "\033[1;1R");
        app.controller(0x1000, 0x1000); // Triangle opens controller keyboard.
        // It covers rows without resizing, so a running program is left undisturbed.
        assert(app.model_.rows() == static_cast<int>(text_metrics::rows));
        assert(app.keyboard_);
        app.controller(0x2000, 0x2000); // Circle closes it.
        assert(!app.keyboard_ && peer.rows == text_metrics::rows);
        // A USB keyboard's Ctrl, Alt and function keys reach the program unchanged.
        peer.input.clear();
        for (char ch : {'b', 'd', 'r', 'a'})
            app.key(Key::control, ch);
        pump();
        assert(peer.input == "\x02\x04\x12\x01"); // tmux prefix, EOF, history, line start
        // Control codes, not CSI-u: vi must see a real Esc from Ctrl+[, and a shell Tab from
        // Ctrl+I.
        peer.input.clear();
        for (char ch : {'[', 'i', 'j', 'm', '@', '^', '_', '?', 'C'})
            app.key(Key::control, ch);
        pump();
        assert(peer.input == std::string("\x1b\t\n\r\0\x1e\x1f\x7f\x03", 9));
        // Ctrl+C stays in order with what was typed before it.
        peer.input.clear();
        app.key(Key::character, 'x');
        app.key(Key::interrupt);
        pump();
        assert(peer.input == "x\x03");
        // Modified arrows reach the program.
        peer.input.clear();
        app.model_.feed("\033[?1l");
        app.key(Key::right, static_cast<char>(TerminalModel::control));
        pump();
        assert(peer.input == "\033[1;5C");
        // Unsplit, Ctrl+Shift+Left/Right are not divider keys: the program gets them.
        peer.input.clear();
        app.key(Key::divider_left);
        app.key(Key::divider_right);
        pump();
        assert(peer.input == "\033[1;6D\033[1;6C");
        // In a running Linux program every F-key is the program's: F2/F4/F6 too.
        peer.input.clear();
        app.key(Key::reveal_password, from_function_key);
        app.key(Key::toggle_keyboard, from_function_key);
        app.key(Key::settings, from_function_key);
        pump();
        assert(peer.input == "\033OQ\033OS\033[17~" && !app.settings_open_ && !app.keyboard_);
        // Ctrl+Shift+S / K (no function-key mark) still reach the app.
        app.key(Key::settings);
        assert(app.settings_open_);
        app.key(Key::settings);
        app.key(Key::toggle_keyboard);
        assert(app.keyboard_);
        app.key(Key::toggle_keyboard);
        // Modified F-keys and editing keys keep their modifiers.
        peer.input.clear();
        app.key(Key::function, static_cast<char>(5 | (4 << 4))); // Ctrl+F5
        app.key(Key::backspace, 2);                              // Alt+Backspace
        app.key(Key::insert);
        pump();
        assert(peer.input == "\033[15;5~\033\x7f\033[2~");
        // With one tab, Ctrl+PgDn is the program's, not a tab switch.
        peer.input.clear();
        app.key(Key::next_tab);
        pump();
        assert(peer.input == "\033[6;5~");
        app.model_.feed("\033[?1h");
        // F5 belongs to a running program and must not destroy its session.
        peer.input.clear();
        app.key(Key::reconnect);
        pump();
        assert(peer.input == "\033[15~" && app.connection_.connected());
        // Page keys go to the program on either screen; Shift+Page views scrollback.
        peer.input.clear();
        app.key(Key::pagedown);
        pump();
        assert(peer.input == "\033[6~" && app.scroll_ == 0);
        peer.input.clear();
        app.key(Key::pageup, static_cast<char>(TerminalModel::shift));
        pump();
        assert(peer.input.empty());
        app.scroll_ = 0;
        // Pasted lines keep their structure; control bytes cannot end the paste early.
        peer.input.clear();
        app.model_.feed("\033[?2004h");
        const char lines[] = "one\n\ttwo\x1b[201~";
        std::memcpy(app.clipboard_.buffer(), lines, sizeof(lines) - 1);
        app.clipboard_.resize(sizeof(lines) - 1);
        app.key(Key::paste);
        pump();
        assert(peer.input == "\033[200~one\r\ttwo[201~\033[201~");
        // Long pastes are not cut to the line editor's length.
        peer.input.clear();
        app.model_.feed("\033[?2004l");
        std::memset(app.clipboard_.buffer(), 'p', 3000);
        app.clipboard_.resize(3000);
        app.key(Key::paste);
        for (int i = 0; i < 2000 && peer.input.size() < 3000; ++i)
            pump();
        assert(peer.input == std::string(3000, 'p'));
        // A program that asked for the mouse gets it; Shift keeps terminal selection.
        peer.input.clear();
        app.model_.feed("\033[?1000h\033[?1006h");
        app.pointer_.x = 96 + 2 * text_metrics::cell_width + 1;
        app.pointer_.y = 222 + 1;
        {
            MouseSample down{};
            down.connected = true;
            down.buttons = 1;
            app.mouse(down, 1000000);
            MouseSample up{};
            up.connected = true;
            app.mouse(up, 1000001);
        }
        pump();
        assert(peer.input == "\033[<0;3;1M\033[<0;3;1m");
        peer.input.clear();
        // Mouse-only users: after Select text from the menu, a click selects, not clicks.
        app.menu_action(ui::MenuItem::select_text);
        {
            MouseSample down{};
            down.connected = true;
            down.buttons = 1;
            app.mouse(down, 2000000);
            MouseSample up{};
            up.connected = true;
            app.mouse(up, 2000001);
        }
        pump();
        assert(peer.input.empty());
        app.key(Key::escape);
        app.model_.feed("\033[?1006l\033[?1000l");
        // Attributes reach the cells the renderer needs.
        app.model_.feed("\033[H\033[2J\033[44mA\033[0mB\033[4mC\033[0m");
        assert(!app.model_.cell(0, 0).default_background &&
               app.model_.cell(0, 1).default_background);
        assert(app.model_.cell(0, 2).underline);
        app.model_.feed("\033[2J\033[H");
        peer.input.clear();
        app.key(Key::alt, 'x');
        pump();
        assert(peer.input == "\033x");
        peer.input.clear();
        app.key(Key::function, 1);
        pump();
        assert(peer.input == "\033OP");
        // A controller alone can drive a full-screen program: move to the terminal
        // row (index 48 onward), then Esc, Ctrl+C one-shot and arrows.
        peer.input.clear();
        const auto press = [&](std::uint32_t buttons) { app.controller(buttons, buttons); };
        press(0x1000); // Open.
        assert(app.selected_ < 48);
        app.selected_ = 0;
        for (int i = 0; i < 4; ++i)
            press(0x0040); // Down to the terminal row.
        assert(app.selected_ == 48);
        press(0x4000); // Esc.
        pump();
        assert(peer.input == "\033");
        peer.input.clear();
        press(0x0020);
        press(0x4000); // Tab.
        pump();
        assert(peer.input == "\t");
        peer.input.clear();
        press(0x0020);
        press(0x4000); // Ctrl latches.
        assert(app.osk_modifiers_ == TerminalModel::control);
        app.selected_ = 24; // 'a' starts the home row of the unshifted layout.
        press(0x4000);
        pump();
        assert(peer.input == "\x01" && app.osk_modifiers_ == 0); // One-shot: released after use.
        peer.input.clear();
        app.selected_ = 54;
        press(0x4000); // Up, in application cursor mode set above.
        pump();
        assert(peer.input == "\033OA");
        peer.input.clear();
        press(0x0040); // Down to the F-key row, then wraps back to the first row.
        assert(app.selected_ == 66);
        press(0x0040);
        assert(app.selected_ == 6);
        press(0x0010);
        assert(app.selected_ == 66);
        // The last row reaches programs that need F-keys, Delete and Shift+Tab.
        app.selected_ = 69; // F10, which quits htop
        press(0x4000);
        pump();
        assert(peer.input == "\033[21~");
        peer.input.clear();
        app.selected_ = 70; // Del
        press(0x4000);
        pump();
        assert(peer.input == "\033[3~");
        peer.input.clear();
        app.shift_ = true; // R3 Shift applies to terminal keys: Shift+Tab
        app.selected_ = 49;
        press(0x4000);
        pump();
        assert(peer.input == "\033[Z");
        app.shift_ = false;
        peer.input.clear();
        app.selected_ = 54;
        press(0x2000); // Close.
        // With no keyboard open, a full-screen program gets the D-pad and page keys.
        peer.input.clear();
        app.model_.feed("\033[?1049h");
        press(0x0010);
        press(0x0040);
        pump();
        assert(peer.input == "\033OA\033OB" && app.scroll_ == 0);
        peer.input.clear();
        app.key(Key::pagedown);
        pump();
        assert(peer.input == "\033[6~");
        app.model_.feed("\033[?1049l");
        peer.input.clear();
        press(0x0010);
        assert(app.scroll_ == 3 && peer.input.empty()); // A normal screen scrolls instead.
        app.scroll_ = 0;
        // Reaping must not discard output which is still readable.
        peer.output = "\r\nFINAL-OUTPUT";
        peer.exited = true;
        pump();
        assert(app.connection_.failed());
        bool found = false;
        for (int r = 0; r < app.model_.rows(); ++r)
        {
            std::string line;
            for (int c = 0; c < app.model_.columns(); ++c)
                line += static_cast<char>(app.model_.cell(r, c).codepoints[0]);
            if (line.find("FINAL-OUTPUT") != line.npos)
                found = true;
        }
        assert(found);
        // Pane resizing reaches an inactive PTY and restores it on unsplit.
        {
            Peer split_peer;
            App split_app({&split_peer, start, poll, stop, write, read, resize, signal});
            split_app.connection_.tick(split_app.model_, 0);
            split_app.sync_terminal_size();
            const auto full = split_peer.cols;
            split_app.toggle_split();
            assert(split_app.active_tab_ == 1 && split_peer.cols < full);
            assert(split_peer.cols == static_cast<unsigned>(split_app.model_.columns()));
            assert(split_peer.rows == static_cast<unsigned>(split_app.model_.rows()));
            split_app.model_.feed("\033[?1000h\033[?1006h");
            split_peer.input.clear();
            split_app.pointer_.x = 110;
            split_app.pointer_.y = 240;
            split_app.mouse({0, 0, 0, 1, true}, 1000000);
            split_app.mouse({1, 0, 0, 1, true}, 1000100);
            split_app.mouse({0, 0, 0, 0, true}, 1000200);
            for (int i = 0; i < 10; ++i)
                split_app.connection_.tick(split_app.model_, i);
            assert(split_peer.input.empty()); // focus click never reaches the program
            split_app.mouse({0, 0, 0, 1, true}, 1100000);
            split_app.mouse({0, 0, 0, 0, true}, 1100100);
            for (int i = 0; i < 10; ++i)
                split_app.connection_.tick(split_app.model_, i);
            assert(split_peer.input == "\033[<0;2;1M\033[<0;2;1m");
            split_app.focus_other_pane();
            split_app.toggle_split();
            assert(split_peer.cols == full && split_app.active_tab_ == 1);
        }
        std::puts("app VT mode, paste, reply, resize and final-drain tests passed");
    }
};
} // namespace term
int main()
{
    term::AppTest::run();
}
