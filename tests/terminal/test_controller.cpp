// PS5Term - controller workflow regression checks. GPL-3.0-or-later.
#include "app.hpp"
#include "i18n.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>
#include <cstdlib>
#include <unistd.h>
namespace term
{
struct AppTest
{
    static bool model_empty(const App &app)
    {
        for (int c = 0; c < app.model_.columns(); ++c)
            if (app.model_.cell(0, c).codepoints[0] != 0)
                return false;
        return true;
    }
    static bool model_contains(const App &app, std::string_view value)
    {
        auto rows = app.model_.rows();
        auto cols = app.model_.columns();
        for (int r = 0; r < rows; ++r)
        {
            std::string line;
            for (int c = 0; c < cols; ++c)
            {
                auto cell = app.model_.cell(r, c);
                auto cp = cell.codepoints[0];
                line += (cp >= 0x20 && cp <= 0x7e) ? static_cast<char>(cp) : ' ';
            }
            if (line.find(value) != std::string::npos)
                return true;
        }
        auto hs = app.model_.history_size();
        auto hf = app.model_.history_first();
        for (std::uint64_t i = 0; i < hs; ++i)
        {
            std::string line;
            for (int c = 0; c < cols; ++c)
            {
                auto cell = app.model_.history_cell(hf + i, c);
                auto cp = cell.codepoints[0];
                line += (cp >= 0x20 && cp <= 0x7e) ? static_cast<char>(cp) : ' ';
            }
            if (line.find(value) != std::string::npos)
                return true;
        }
        return false;
    }
    static void run()
    {
        App app;
        app.model_.feed("\033[20h"); // LNM mode
        // Circle while offline must not write fake Ctrl+C output.
        app.controller(0x2000, 0x2000);
        assert(model_empty(app));
        app.key(Key::character, 'x');
        app.controller(0x2000, 0x2000);
        assert(app.editor_.text().empty());
        // Cross opens typing on an empty line; select, shift and delete work.
        app.controller(0x4000, 0x4000);
        assert(app.keyboard_);
        app.controller(0x4000, 0x4000);
        assert(app.editor_.text() == "1");
        app.controller(0x8000, 0x8000);
        assert(app.editor_.text().empty());
        app.controller(0x0004, 0x0004);
        app.controller(0x4000, 0x4000);
        assert(app.editor_.text() == "!");
        app.controller(0x2000, 0x2000);
        assert(!app.keyboard_ && app.editor_.text() == "!");
        // Options opens Settings, Circle goes back and preserves entered text.
        app.controller(0x0008, 0x0008);
        assert(app.settings_open_);
        app.controller(0x1000, 0x1000);
        assert(app.password_visible_);
        app.controller(0x2000, 0x2000);
        assert(!app.password_visible_);
        assert(!app.settings_open_ && app.editor_.text() == "!");
        app.key(Key::settings);
        app.key(Key::reveal_password);
        assert(app.password_visible_);
        app.key(Key::settings);
        assert(!app.password_visible_);
        // Neither Circle nor the explicit interrupt chord prints a fake ^C offline.
        app.controller(0x2000, 0x2100);
        assert(model_empty(app));
        keyboard_navigation();
        keyboard_selection_and_tabs();
        subsystem_navigation();
        environments_page();
        about_page();
        clipboard_controller();
        terminal_search();
        session_tabs();
        split_panes();
        mouse();
    }
    static void split_panes()
    {
        App app;
        app.sync_terminal_size();
        const int full = app.model().columns();
        app.editor().insert("left draft");
        app.toggle_split();
        assert(app.split_ && app.active_tab_ == 1);
        assert(app.panes_[0] == 0 && app.panes_[1] == 1);
        assert(app.model_.columns() < full && app.model().columns() == app.model_.columns());
        assert(app.grid().left > 960);
        assert(app.grid().area().x + app.grid().area().w <= 1824);
        app.editor().insert("right draft");
        app.key(Key::focus_pane);
        assert(app.active_tab_ == 0 && app.editor().text() == "left draft");
        assert(app.grid().left == 96);
        app.key(Key::search);
        app.key(Key::character, 'x');
        app.focus_other_pane();
        assert(!app.search_open_ && app.editor().text() == "right draft");
        // Focus consumes a click, even if the newly focused program requests mouse reports.
        app.pointer_.x = 110;
        app.pointer_.y = 240;
        app.mouse({0, 0, 0, 1, true}, 100000);
        assert(app.active_tab_ == 0 && !app.selection_.active());
        app.mouse({1, 0, 0, 1, true}, 105000);
        assert(!app.selection_.active() &&
               app.drag_mode_ == 0); // held focus click remains consumed
        app.mouse({0, 0, 0, 0, true}, 110000);
        app.focus_other_pane();
        app.keyboard_ = true;
        const int rows = app.model().rows();
        app.sync_terminal_size();
        assert(app.model().rows() == rows && app.grid().rows < static_cast<unsigned>(rows));
        app.keyboard_ = false;
        // The divider moves by keyboard, menu and mouse drag; both panes resize, clamped.
        {
            const int left_cols = app.model_.columns(), right_cols = app.tabs_[0]->model.columns();
            app.key(Key::divider_right);
            assert(app.split_left_ == ui::split_default + 80);
            assert(app.model_.columns() > left_cols && app.tabs_[0]->model.columns() < right_cols);
            assert(app.grid_for(1).left == 96 + app.split_left_ + 68);
            app.menu_action(ui::MenuItem::divider_left);
            assert(app.split_left_ == ui::split_default);
            assert(app.model_.columns() == left_cols &&
                   app.tabs_[0]->model.columns() == right_cols);
            for (int i = 0; i < 30; ++i)
                app.key(Key::divider_left);
            assert(app.split_left_ == ui::split_min);
            for (int i = 0; i < 30; ++i)
                app.key(Key::divider_right);
            assert(app.split_left_ == ui::split_total - ui::split_min);
            // Pane text never runs under the other pane or off screen.
            for (unsigned pane : app.panes_)
            {
                const auto area = app.grid_for(pane).area();
                assert(area.x + area.w <= 1816);
            }
            assert(app.grid_for(0).area().x + app.grid_for(0).area().w <
                   ui::divider_x(app.split_left_));
            assert(app.grid_for(1).left > ui::divider_x(app.split_left_));
            app.move_divider(ui::split_default);
            // Drag: press on the divider, move, release; terminals resize once on release.
            const unsigned active = app.active_tab_;
            app.pointer_.x = ui::divider_x(ui::split_default);
            app.pointer_.y = 400;
            app.mouse({0, 0, 0, 1, true}, 200000);
            assert(app.divider_drag_ >= 0 && app.active_tab_ == active && !app.selection_.active());
            app.pointer_.x = 700;
            app.mouse({1, 0, 0, 1, true}, 201000);
            assert(app.split_left_ == ui::split_default); // not yet applied
            app.mouse({0, 0, 0, 0, true}, 202000);
            assert(app.divider_drag_ < 0);
            assert(app.split_left_ >= 560 && app.split_left_ <= 570);
            assert(app.model_.columns() < left_cols);
            app.move_divider(ui::split_default);
            assert(app.model_.columns() == left_cols);
        }
        // A hidden tab replaces only the focused pane, leaving its neighbor running.
        assert(app.new_tab());
        assert(app.panes_[0] == 0 && app.panes_[1] == 2);
        assert(app.tabs_[0]->model.columns() == full);
        app.close_tab();
        assert(!app.split_ && app.active_tab_ == 0 && app.model().columns() == full);
        app.toggle_split();
        assert(app.split_ && app.active_tab_ == 1);
        app.key(Key::toggle_split);
        assert(!app.split_ && app.active_tab_ == 1 && app.model().columns() == full);
        // Primary close is harmless and does not close either split shell.
        app.toggle_split();
        app.switch_tab(0);
        app.close_tab();
        assert(app.split_ && app.tabs_[0]);
    }
    static void session_tabs()
    {
        App app;
        app.editor_.insert("primary draft");
        app.model_.feed("primary marker");
        assert(app.new_tab());
        assert(app.active_tab_ == 1);
        app.editor().insert("second draft");
        app.model().feed("second marker");
        assert(app.editor_.text() == "primary draft");
        app.switch_tab(0);
        assert(app.editor().text() == "primary draft");
        assert(model_contains(app, "primary marker"));
        assert(!model_contains(app, "second marker"));
        app.switch_tab(1);
        assert(app.editor().text() == "second draft");
        assert(app.new_tab() && app.new_tab());
        assert(!app.new_tab()); // four total, no eviction
        app.close_tab();
        assert(app.active_tab_ == 0);
        app.switch_tab(1);
        assert(app.editor().text() == "second draft");
        app.close_tab();
        assert(!app.tabs_[0]);
        app.close_tab(); // primary cannot be closed
        assert(app.editor().text() == "primary draft");
        app.key(Key::new_tab);
        assert(app.active_tab_ == 1);
        app.key(Key::previous_tab);
        assert(app.active_tab_ == 0);
        app.key(Key::next_tab);
        assert(app.active_tab_ == 1);
        app.key(Key::close_tab);
        assert(app.active_tab_ == 0);
    }
    static void terminal_search()
    {
        App app;
        app.feed("needle first\r\nother\r\nneedle last");
        app.editor_.insert("keep draft");
        app.key(Key::search);
        for (char ch : std::string("needle"))
            app.key(Key::character, ch);
        app.search_.step(app.model_, 10000);
        assert(app.search_.count() == 2);
        app.key(Key::enter);
        assert(app.editor_.text() == "keep draft");
        assert(app.search_open_);
        std::array<char, 128> pending{};
        assert(app.model_.take_output(pending) == 0); // Enter never goes to a PTY.
        app.search_key(Key::clear, 0);
        app.clipboard_.set("é界");
        app.search_key(Key::paste, 0);
        assert(app.search_editor_.text() == "é界");
        app.search_key(Key::backspace, 0);
        assert(app.search_editor_.text() == "é");
        app.search_key(Key::clear, 0);
        for (char ch : std::string("needle"))
            app.key(Key::character, ch);
        app.controller(0x1000, 0x1000); // Triangle opens query keyboard
        assert(app.keyboard_);
        app.type_key(0); // Search only; must not reach Linux or the native line editor.
        assert(app.search_editor_.text() == "needle1");
        assert(app.editor_.text() == "keep draft");
        app.controller(0x2000, 0x2000);
        assert(!app.search_open_ && !app.keyboard_);
        app.menu_action(ui::MenuItem::search);
        app.pointer_.x = ui::search_close.x + 10;
        app.pointer_.y = ui::search_close.y + 10;
        app.left_press(1);
        assert(!app.search_open_);
        app.key(Key::search);
        app.key(Key::settings);
        assert(!app.search_open_ && app.settings_open_);
    }
    static void subsystem_navigation()
    {
        App app;
        app.key(Key::settings);
        // Focus follows the screen top to bottom: SSH, Retry, then the Linux block.
        app.key(Key::tab);
        assert(app.settings_focus_ == 3);
        app.key(Key::tab);
        assert(app.settings_focus_ == 6);
        app.key(Key::enter); // Unbundled host profile stays native, no misleading enabled state.
        assert(app.connection_.environment() == Connection::Environment::console);
        // The /mnt/ps5 choice is reachable from every input when it is shown, and
        // skipped when this build has no shared folder (no invisible focus).
        const bool shares =
            Connection::linux_available() && platform::LinuxSubsystem::shared_folder_available();
        app.controller(0x0040, 0x0040); // Controller has the same focus traversal.
        if (shares)
        {
            assert(app.settings_focus_ == 7);
            assert(app.focused(ui::host_mount_switch));
            app.controller(0, 0);
            app.controller(0x0040, 0x0040);
        }
        assert(app.settings_focus_ == 10);
        assert(app.focused(ui::environments_button));
        app.controller(0x4000, 0x4000); // Cross opens the Linux environments screen...
        assert(app.settings_open_ && app.environments_open_);
        app.controller(0, 0);
        app.controller(0x2000, 0x2000); // ...and Circle comes back to Settings.
        assert(app.settings_open_ && !app.environments_open_ && app.settings_focus_ == 10);
        app.controller(0, 0);
        app.controller(0x0040, 0x0040); // Language, beside Text size on the same row.
        assert(app.settings_focus_ == 13 && app.focused(ui::language_button));
        assert(i18n::selected() == i18n::automatic);
        app.controller(0x4000, 0x4000); // Cross cycles Auto -> English -> the next language.
        assert(i18n::selected() == i18n::english && i18n::current() == i18n::english);
        app.controller(0, 0);
        i18n::select(i18n::automatic); // Leave the rest of the tests in English.
        app.controller(0x0040, 0x0040);
        assert(app.settings_focus_ == 9);
        assert(app.focused(ui::text_size_button));
        app.controller(0x4000, 0x4000);
        assert(app.settings_open_ && app.text_style_ == 3);
        assert(app.model_.columns() == 114 && app.model_.rows() == 19);
        assert(app.grid().cell_width == 15 && app.grid().row_height == 30);
        app.controller(0x4000, 0x4000);
        assert(app.text_style_ == 1 && app.model_.columns() == 240 && app.model_.rows() == 36);
        for (unsigned expected : {4U, 5U, 1U, 2U, 11U})
        {
            app.controller(0x0040, 0x0040);
            assert(app.settings_focus_ == expected);
        }
        app.controller(0x4000, 0x4000);
        assert(!app.settings_open_);
        assert(ui::settings_button_at(1400, 670) == ui::SettingsButton::toggle_linux);
        assert(ui::settings_button_at(800, 730) == ui::SettingsButton::environments);
        assert(ui::settings_button_at(1400, 730) == ui::SettingsButton::none);
        assert(ui::settings_button_at(300, 730) == ui::SettingsButton::toggle_host_mount);
    }
    // Linux environments: focus skips what is not shown, every input acts and goes back, and
    // removing asks first. This host build has no Linux, so nothing really starts.
    static void environments_page()
    {
        using ui::EnvButton;
        App app;
        std::uint64_t now = 10000000;
        app.key(Key::settings);
        app.settings_action(ui::SettingsButton::environments);
        assert(app.environments_open_ && app.environments_focus_ == EnvButton::open_alpine);
        assert(!app.environments_.known);
        // Unknown state: only Open and Back (Linux and storage controls need a Linux build).
        app.key(Key::tab);
        assert(app.environments_focus_ == EnvButton::open_ubuntu && app.focused(ui::env_open(2)));
        app.key(Key::tab);
        assert(app.environments_focus_ == EnvButton::back);
        app.key(Key::tab);
        assert(app.environments_focus_ == EnvButton::open_alpine);
        app.key(Key::backtab);
        assert(app.environments_focus_ == EnvButton::back);
        app.controller(0x0040, 0x0040); // D-pad down wraps to the first control.
        assert(app.environments_focus_ == EnvButton::open_alpine);
        app.controller(0, 0);
        app.controller(0x4000, 0x4000); // Cross: Open cannot start Linux here and says so.
        assert(app.settings_open_ && app.environments_open_ && app.environment_problem_ &&
               app.environment_message_[0]);
        app.controller(0, 0);
        // What the guest reports decides the rest: Ubuntu installed and the default.
        assert(parse_linux_environments(
            R"({"default":"ubuntu","data":true,"disk_kib":1000000,"used_kib":400000,"environments":[)"
            R"({"name":"alpine","state":"absent","kib":0},{"name":"ubuntu","state":"installed","kib":338534}]})",
            app.environments_));
        assert(app.environment_shown(EnvButton::remove_ubuntu) &&
               !app.environment_shown(EnvButton::default_ubuntu) && // already the default
               app.environment_shown(EnvButton::default_base) &&
               !app.environment_shown(EnvButton::remove_alpine) &&
               !app.environment_shown(EnvButton::default_alpine));
        app.environments_focus_ = EnvButton::open_ubuntu;
        app.key(Key::right);
        assert(app.environments_focus_ == EnvButton::remove_ubuntu);
        app.now_ = now;
        app.key(Key::enter); // Removing asks again first and changes nothing yet.
        assert(app.environment_armed_ == EnvButton::remove_ubuntu &&
               app.environment_job_ == App::EnvironmentJob::none && app.environments_.installed[2]);
        // Making the current default the default again does nothing.
        app.environment_action(EnvButton::default_ubuntu);
        assert(app.environment_job_ == App::EnvironmentJob::none);
        // Mouse: a click on Back returns to Settings; right-click does too.
        click(app, ui::help_back.x + 4, ui::help_back.y + 4, now);
        assert(app.settings_open_ && !app.environments_open_);
        app.settings_action(ui::SettingsButton::environments);
        click(app, 900, 600, now, 2);
        assert(app.settings_open_ && !app.environments_open_);
        // Keyboard: Esc goes back; Settings closing also closes the screen.
        app.settings_action(ui::SettingsButton::environments);
        app.key(Key::escape);
        assert(app.settings_open_ && !app.environments_open_);
        app.settings_action(ui::SettingsButton::environments);
        app.key(Key::settings);
        assert(!app.settings_open_ && !app.environments_open_);
        // Every button has its own place and the hit test finds it.
        for (auto button :
             {EnvButton::default_base, EnvButton::open_alpine, EnvButton::default_alpine,
              EnvButton::remove_alpine, EnvButton::open_ubuntu, EnvButton::default_ubuntu,
              EnvButton::remove_ubuntu, EnvButton::grow_1, EnvButton::grow_2, EnvButton::grow_4,
              EnvButton::delete_backup, EnvButton::delete_disk, EnvButton::toggle_linux,
              EnvButton::back})
        {
            const auto r = ui::env_button_rect(button);
            assert(ui::env_button_at(r.x + 2, r.y + 2) == button);
        }
    }
    // About opens from Settings with every input and always returns to Settings.
    static void about_page()
    {
        App app;
        app.key(Key::settings);
        app.controller(0x0010, 0x0010); // D-pad Up from the first item wraps to About.
        assert(app.settings_focus_ == 12 && app.focused(ui::help_about));
        app.controller(0, 0);
        app.controller(0x4000, 0x4000); // Cross opens it.
        assert(app.settings_open_ && app.about_open_ && app.focused(ui::help_back));
        app.controller(0, 0);
        app.controller(0x0040, 0x0040); // Read-only: the D-pad changes nothing.
        assert(app.about_open_ && app.settings_focus_ == 12);
        app.controller(0, 0);
        app.controller(0x2000, 0x2000); // Circle goes back to Settings, not the terminal.
        assert(app.settings_open_ && !app.about_open_ && app.settings_focus_ == 12);
        app.controller(0, 0);
        app.controller(0x4000, 0x4000);
        app.controller(0, 0);
        app.controller(0x0008, 0x0008); // Options closes Settings from About.
        assert(!app.settings_open_ && !app.about_open_);
        app.controller(0, 0);

        app.key(Key::settings); // Keyboard: F1 opens, Esc returns, Esc again closes.
        app.key(Key::function, 1);
        assert(app.about_open_);
        app.key(Key::tab);
        assert(app.about_open_ && app.settings_focus_ == 12);
        app.key(Key::escape);
        assert(app.settings_open_ && !app.about_open_);
        app.key(Key::escape);
        assert(!app.settings_open_);
        app.key(Key::settings);
        app.key(Key::function, 1);
        app.key(Key::settings); // Ctrl+Shift+S / F6 closes everything.
        assert(!app.settings_open_ && !app.about_open_);
        app.key(Key::settings);
        assert(!app.about_open_); // Settings always reopens on its own page.

        // Mouse: click the label, then Back or a right-click returns to Settings.
        app.pointer_.x = ui::help_about.x + 10;
        app.pointer_.y = ui::help_about.y + 10;
        app.left_press(1);
        assert(app.about_open_);
        app.left_press(2); // Same spot again: nothing on About is there.
        assert(app.about_open_);
        app.pointer_.x = ui::help_back.x + 10;
        app.pointer_.y = ui::help_back.y + 10;
        app.left_press(3);
        assert(app.settings_open_ && !app.about_open_);
        app.pointer_.x = ui::help_about.x + 10;
        app.pointer_.y = ui::help_about.y + 10;
        app.left_press(4);
        assert(app.about_open_);
        MouseSample right{};
        right.connected = true;
        right.buttons = 2;
        app.mouse(right, 5);
        assert(app.settings_open_ && !app.about_open_);
        assert(ui::settings_button_at(ui::help_about.x + 10, ui::help_about.y + 10) ==
               ui::SettingsButton::about);
    }
    static void clipboard_controller()
    {
        App app;
        app.feed("alpha beta");
        app.controller(0x0002, 0x0002); // L3: actions.
        assert(app.menu_open_);
        app.menu_action(ui::MenuItem::select_text);
        app.menu_open_ = false;
        assert(app.output_selection_);
        app.selection_cursor_ = {app.model_.history_end(), 0};
        app.selection_.begin(app.selection_cursor_, app.selection_cursor_);
        for (int i = 0; i < 4; i++)
            app.controller(0x0020, 0x0020);
        app.controller(0x4000, 0x4000); // Cross copies, never submits.
        assert(!app.output_selection_ && app.clipboard_.text() == "alpha");
        app.controller(0x0002, 0x0002);
        app.menu_action(ui::MenuItem::paste);
        app.menu_open_ = false;
        assert(app.editor_.text() == "alpha");
    }
    static void keyboard_selection_and_tabs()
    {
        // Keyboard-only selection and tabs: Ctrl+Shift+Space selects, Space restarts the
        // selection where the cursor is, Ctrl+Shift+1..4 shows a tab directly.
        App app;
        app.feed("first second");
        app.key(Key::select_text);
        assert(app.output_selection_);
        app.selection_cursor_ = {app.model_.history_end(), 0};
        for (int i = 0; i < 6; ++i)
            app.key(Key::right);
        app.key(Key::character, ' '); // start here: "second"
        for (int i = 0; i < 5; ++i)
            app.key(Key::right);
        app.key(Key::enter);
        assert(!app.output_selection_ && app.clipboard_.text() == "second");
        app.key(Key::new_tab);
        app.key(Key::go_tab, 0);
        assert(app.active_tab_ == 0);
        app.key(Key::go_tab, 1);
        assert(app.active_tab_ == 1);
        app.key(Key::go_tab, 3); // no such tab: stays, with a notice
        assert(app.active_tab_ == 1 && app.tab_notice_[0]);
        app.key(Key::close_tab);
        // Hints name the keys of the input used last.
        app.last_input_ = App::InputKind::controller;
        assert(app.pick("F6", "click", "Options") == "Options");
        app.controller(0x0010, 0x0010);
        assert(app.last_input_ == App::InputKind::controller);
        app.last_input_ = App::InputKind::keyboard;
        assert(app.pick("F6", "click", "Options") == "F6");
        std::puts("PASS: keyboard selection anchor, direct tabs, input-aware hints");
    }
    static void keyboard_navigation()
    {
        App app;
        char directory[] = "/tmp/ps5term-input-XXXXXX";
        assert(mkdtemp(directory));
        const std::string flag = std::string(directory) + "/ssh.enabled";
        app.settings_ = Settings(directory, flag.c_str());
        app.key(Key::settings);
        app.key(Key::enter);
        assert(app.ssh_enabled_ && app.settings_.ssh_enabled());
        std::uint64_t now = 10000000;
        assert(ui::settings_button_at(1400, 250) == ui::SettingsButton::restart_ssh);
        click(app, 1400, 250, now); // explicit SSH retry, distinct from local reconnect
        assert(app.ssh_enabled_ && app.settings_focus_ == 3);
        click(app, 1600, 250, now); // same setting with a mouse, no controller
        assert(!app.ssh_enabled_ && !app.settings_.ssh_enabled());
        assert(rmdir(directory) == 0);
        app.key(Key::backtab); // reverse wrap: About, Back, Change password, Show password
        assert(app.settings_focus_ == 12);
        app.key(Key::backtab);
        app.key(Key::backtab);
        app.key(Key::backtab);
        assert(app.settings_focus_ == 1);
        app.key(Key::enter);
        assert(app.password_visible_);
        app.key(Key::tab); // Change password
        app.key(Key::tab); // Back
        app.key(Key::enter);
        assert(!app.settings_open_ && !app.password_visible_);
        app.key(Key::settings);
        app.key(Key::reveal_password); // F2
        assert(app.password_visible_);
        app.key(Key::reveal_password);
        assert(!app.password_visible_ && app.settings_focus_ == 1); // F2 also moves focus there
        app.key(Key::character, ' ');
        assert(app.password_visible_ && app.editor_.text().empty());
        app.key(Key::escape);
        app.feed("keyboard menu");
        app.key(Key::menu);
        assert(app.menu_open_);
        // Copy and Paste cannot run yet, so focus starts on Select all and skips them.
        assert(app.menu_focus_ == static_cast<unsigned>(ui::MenuItem::select_all));
        app.key(Key::up);
        assert(app.menu_focus_ == static_cast<unsigned>(ui::MenuItem::focus_pane));
        app.key(Key::down);
        assert(app.menu_focus_ == static_cast<unsigned>(ui::MenuItem::select_all));
        app.key(Key::enter);
        assert(!app.menu_open_ && app.selection_.active());
        app.key(Key::copy);
        assert(app.clipboard_.text().find("keyboard menu") != std::string_view::npos);
        app.key(Key::toggle_keyboard);
        assert(app.keyboard_);
        app.key(Key::toggle_keyboard);
        assert(!app.keyboard_);
        // Password field is modal; Tab reaches the Next and Cancel buttons.
        app.settings_open_ = true;
        app.password_entry_.begin();
        app.key(Key::character, 'a');
        app.key(Key::tab);
        app.key(Key::tab);
        app.key(Key::tab);
        app.key(Key::enter);
        assert(!app.password_entry_.confirming()); // invalid short password
        app.key(Key::tab);
        app.key(Key::enter);
        assert(!app.password_entry_.active() && app.editor_.text().empty());
        app.password_entry_.begin();
        app.password_focus_ = 0;
        for (char ch : std::string_view("sample-pass"))
            app.key(Key::character, ch);
        app.key(Key::enter);
        assert(app.password_entry_.confirming());
        app.key(Key::escape);
        assert(!app.password_entry_.active());
        std::puts("PASS: keyboard-only Settings, focus, action menu and password dialog");
    }
    // Moves the pointer to (x, y) with relative samples, like a real mouse.
    static void move_to(App &app, int x, int y, std::uint32_t buttons, std::uint64_t &now)
    {
        while (app.pointer_.x != x || app.pointer_.y != y)
        {
            MouseSample s;
            s.connected = true;
            s.buttons = buttons;
            s.dx = x - app.pointer_.x;
            s.dy = y - app.pointer_.y;
            app.mouse(s, now += 1000);
        }
    }
    static void button(App &app, std::uint32_t buttons, std::uint64_t &now, int wheel = 0)
    {
        MouseSample s;
        s.connected = true;
        s.buttons = buttons;
        s.wheel = wheel;
        app.mouse(s, now += 1000);
    }
    static void click(App &app, int x, int y, std::uint64_t &now, std::uint32_t which = 1)
    {
        move_to(app, x, y, 0, now);
        button(app, which, now);
        button(app, 0, now);
    }
    static void mouse()
    {
        App app;
        std::uint64_t now = 10000000;
        app.model_.feed("\033[20h"); // LNM mode
        app.model_.feed("alpha beta\nsecond line\nthird");
        // Selection and input use the renderer's shared cell geometry.
        auto at = [](std::size_t row, std::size_t col)
        {
            return std::pair<int, int>{96 + static_cast<int>(col) * text_metrics::cell_width + 4,
                                       226 + static_cast<int>(row) * text_metrics::row_height};
        };
        // A plain click does not select; a drag selects whole characters.
        auto [x0, y0] = at(0, 6);
        click(app, x0, y0, now);
        assert(!app.selection_.active() && app.pointer_shown_.shown);
        now += 1000000;
        move_to(app, x0, y0, 0, now);
        button(app, 1, now);
        auto [x1, y1] = at(1, 5);
        move_to(app, x1, y1, 1, now);
        button(app, 0, now);
        assert(app.selection_.active() && app.drag_mode_ == 0);
        // Copy (right-click menu) then middle-click paste into the editor.
        click(app, x1, y1, now, 2);
        assert(app.menu_open_);
        auto copy = app.menu_;
        click(app, copy.x + 20, copy.y + 10, now);
        assert(!app.menu_open_ && app.clipboard_.text() == "beta\nsecond");
        click(app, 900, 500, now, 4);
        assert(app.editor_.text() == "beta second");
        // Double click selects a word, triple click the line.
        now += 1000000;
        auto [x2, y2] = at(1, 2);
        click(app, x2, y2, now);
        click(app, x2, y2, now);
        app.copy();
        assert(app.clipboard_.text() == "second");
        click(app, x2, y2, now);
        app.copy();
        assert(app.clipboard_.text() == "second line");
        // Esc first drops the selection, then behaves as before.
        app.key(Key::escape);
        assert(!app.selection_.active() && app.editor_.text() == "beta second");
        // Clicking the input line moves the text cursor.
        click(app, 128 + text_metrics::cell_width * 4, 880, now);
        assert(app.editor_.cursor() == 4);
        // Keyboard copy/paste and Select all / Clear from the menu.
        app.key(Key::paste);
        assert(app.editor_.text() == "betasecond line second");
        click(app, 600, 400, now, 2);
        click(app, app.menu_.x + 20, app.menu_.y + 4 + 44 * 2 + 10, now); // Select all
        app.key(Key::copy);
        assert(app.clipboard_.text().substr(0, 28) == "alpha beta\nsecond line\nthird");
        // Circle closes the menu without clearing the editor.
        click(app, 600, 400, now, 2);
        app.controller(0x2000, 0x2000);
        assert(!app.menu_open_ && !app.editor_.text().empty());
        click(app, 600, 400, now, 2);
        click(app, app.menu_.x + 20, app.menu_.y + 4 + 44 * 3 + 10, now); // Clear
        assert(!app.selection_.active());
        // Wheel scrolls output 3 lines a notch, a page with Shift.
        for (int i = 0; i < 100; ++i)
            app.model_.feed("row\n");
        button(app, 0, now, 1);
        assert(app.scroll_ == 3);
        app.keyboard_mods_ = 0x02;
        button(app, 0, now, 1);
        assert(app.scroll_ == 15);
        app.keyboard_mods_ = 0;
        button(app, 0, now, -2);
        assert(app.scroll_ == 9);
        // On-screen keyboard: clicking a key types it; Shift/Delete buttons.
        app.editor_.clear();
        click(app, 800, 170, now); // visible Keyboard button; no controller needed
        assert(app.keyboard_);
        click(app, 84 + 147 * 2 + 50, 636 + 15, now); // '3'
        click(app, 1190, 612, now);                   // Shift
        click(app, 84 + 147 * 2 + 50, 636 + 15, now); // '#'
        assert(app.editor_.text() == "3#" && app.selected_ == 2);
        click(app, 1300, 612, now); // Delete
        assert(app.editor_.text() == "3");
        // Settings: open by clicking, speed buttons, right-click = back.
        click(app, 800, 170, now);
        assert(!app.keyboard_);
        click(app, 1500, 168, now);
        assert(app.settings_open_);
        auto speed = app.speed_;
        click(app, 1700, 885, now);
        assert(app.speed_ == speed + 1);
        app.controller(0x0080, 0x0080); // D-pad left
        assert(app.speed_ == speed);
        click(app, 90, 985, now); // Show/hide password
        assert(app.password_visible_);
        click(app, 900, 500, now, 2);
        assert(!app.settings_open_ && !app.password_visible_);
        // Password modal is fully mouse-accessible, including confirmation/cancel.
        click(app, 1500, 168, now);
        app.password_entry_.begin();
        for (int i = 0; i < 8; ++i)
            click(app, 84 + 50, 636 + 15, now);
        click(app, 1410, 615, now);
        assert(app.password_entry_.confirming());
        click(app, 100, 1030, now); // Cancel
        assert(!app.password_entry_.active() && app.settings_open_);
        click(app, 900, 500, now, 2);
        // Controller use hides an idle pointer; a disconnect drops buttons.
        now += 2000000;
        app.now_ = now;
        app.controller(0x0010, 0x0010);
        assert(!app.pointer_shown_.shown);
        button(app, 1, now);
        MouseSample gone;
        app.mouse(gone, now += 1000);
        assert(app.mouse_buttons_ == 0 && app.drag_mode_ == 0);
        std::puts("PASS: mouse select/copy/paste, menu, word/line clicks, cursor, wheel, "
                  "on-screen keyboard, settings clicks, pointer hiding");
    }
};
} // namespace term
int main()
{
    term::AppTest::run();
    std::puts(
        "PASS: controller typing, shift/delete, safe cancel, Settings/back, offline interrupt");
}
