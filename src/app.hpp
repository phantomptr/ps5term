// PS5Term - terminal scene. GPL-3.0-or-later.
#pragma once
#include "connection.hpp"
#include "demo_renderer.hpp"
#include "input.hpp"
#include "linux_environments.hpp"
#include "mouse.hpp"
#include "pointer.hpp"
#include "research.hpp"
#include "ssh_service.hpp"
#include "settings.hpp"
#include "password_entry.hpp"
#include "terminal.hpp"
#include "terminal_search.hpp"
#include <memory>
#include <new>
#if defined(PS5TERM_OWNED_SSH)
#include "../backend/owned_ssh/guest_access.hpp"
#endif
namespace term
{
class App;
#if defined(PS5TERM_OWNED_SSH)
// SSH's way into the Linux guest. A "linux" login starts the guest if needed.
class AppGuest final : public ssh::GuestAccess
{
  public:
    explicit AppGuest(App &app) noexcept : app_(app)
    {
    }
    int open(unsigned columns, unsigned rows, std::string_view term, std::string_view command,
             bool terminal) noexcept override;
    std::size_t write(int handle, std::string_view bytes) noexcept override;
    std::size_t read(int handle, char *bytes, std::size_t capacity) noexcept override;
    void resize(int handle, unsigned columns, unsigned rows) noexcept override;
    void finish_input(int handle) noexcept override;
    void close(int handle) noexcept override;
    int poll(int handle, int *status) noexcept override;

  private:
    App &app_;
};
#endif
class App
{
  public:
    explicit App(platform::LauncherApi launcher = {}) noexcept : connection_(launcher)
    {
#if defined(PS5TERM_OWNED_SSH)
        ssh_.set_guest(&guest_);
#endif
    }
    void tick(std::uint64_t now) noexcept;
    void key(Key key, char character = 0) noexcept;
    void controller(std::uint32_t pressed, std::uint32_t held) noexcept;
    void mouse(const MouseSample &sample, std::uint64_t now) noexcept;
    void draw(ps5::demo::Canvas &canvas) noexcept;
    void welcome() noexcept;
    void feed(std::string_view text) noexcept
    {
        model().feed(text);
    }
    void research_keyboard(std::uint32_t mods, const std::uint16_t *keys, int count, bool connected,
                           bool intercepted) noexcept
    {
        if (!password_entry_.active())
            research_.keyboard(mods, keys, count, connected, intercepted);
    }
    // The input used last decides which keys the on-screen hints name. A PS5 starts
    // with the controller; a keyboard or mouse takes over as soon as it is used.
    enum class InputKind : unsigned char
    {
        controller,
        keyboard,
        mouse
    };
    InputKind last_input_ = InputKind::controller;

  private:
    friend struct AppTest;
#if defined(PS5TERM_OWNED_SSH)
    friend class AppGuest;
    AppGuest guest_{*this};
#endif
    TerminalModel model_{static_cast<int>(text_metrics::rows),
                         static_cast<int>(text_metrics::columns)};
    Editor editor_;
    SearchQuery search_editor_;
    TerminalSearch search_;
    bool search_open_ = false, search_selected_ = false;
    std::size_t search_index_ = 0;
    void open_search() noexcept;
    void close_search() noexcept;
    void search_key(Key, char) noexcept;
    void refresh_search() noexcept;
    void search_move(int) noexcept;
    void reveal_search() noexcept;
    void draw_search(ps5::demo::Canvas &) noexcept;

    Connection connection_;
    struct Tab
    {
        TerminalModel model;
        Editor editor;
        Connection connection;
        std::size_t scroll = 0;
        Tab(int rows, int cols, platform::GuestAgent *peer, const char *command = nullptr) noexcept
            : model(rows, cols), connection(peer ? Connection(peer, command) : Connection())
        {
        }
    };
    // Additional tabs allocate only when opened; never another Linux VM.
    std::array<std::unique_ptr<Tab>, 3> tabs_{};
    unsigned active_tab_ = 0;
    bool split_ = false;
    std::array<unsigned, 2> panes_{0, 0};
    int split_left_ = ui::split_default; // left pane width in pixels
    int divider_drag_ = -1;              // pointer x while dragging the divider, else -1
    void move_divider(int left) noexcept;
    int pane_width(unsigned index) const noexcept;
    void toggle_split() noexcept;
    void focus_other_pane() noexcept;
    bool focus_pointer_pane() noexcept;
    ui::Grid grid_for(unsigned) const noexcept;
    void draw_output(ps5::demo::Canvas &, unsigned) noexcept;
    TerminalModel &model_at(unsigned i) noexcept
    {
        return i ? tabs_[i - 1]->model : model_;
    }
    const TerminalModel &model_at(unsigned i) const noexcept
    {
        return i ? tabs_[i - 1]->model : model_;
    }
    Connection &connection_at(unsigned i) noexcept
    {
        return i ? tabs_[i - 1]->connection : connection_;
    }
    std::size_t &saved_scroll(unsigned i) noexcept
    {
        return i ? tabs_[i - 1]->scroll : primary_scroll_;
    }

    std::size_t primary_scroll_ = 0;
    const char *tab_notice_ = "";
    TerminalModel &model() noexcept
    {
        return active_tab_ ? tabs_[active_tab_ - 1]->model : model_;
    }
    const TerminalModel &model() const noexcept
    {
        return active_tab_ ? tabs_[active_tab_ - 1]->model : model_;
    }
    Editor &editor() noexcept
    {
        return active_tab_ ? tabs_[active_tab_ - 1]->editor : editor_;
    }
    const Editor &editor() const noexcept
    {
        return active_tab_ ? tabs_[active_tab_ - 1]->editor : editor_;
    }
    Connection &active_connection() noexcept
    {
        return active_tab_ ? tabs_[active_tab_ - 1]->connection : connection_;
    }
    const Connection &active_connection() const noexcept
    {
        return active_tab_ ? tabs_[active_tab_ - 1]->connection : connection_;
    }
    bool new_tab() noexcept;
    void close_tab() noexcept;
    void switch_tab(unsigned) noexcept;
    // A Linux program is on screen with no app dialog open: F-keys are the program's.
    // The hint for the input used last.
    std::string_view pick(std::string_view keyboard, std::string_view mouse,
                          std::string_view controller) const noexcept;
    bool confirmed(ui::SettingsButton button) noexcept;
    void move_settings_focus(int direction) noexcept;
    bool menu_enabled(ui::MenuItem item) const noexcept;
    void move_menu_focus(int direction) noexcept;
    bool program_owns_function_keys() const noexcept;
    unsigned open_tabs() const noexcept;
    void cycle_tab(int) noexcept;
    void draw_tabs(ps5::demo::Canvas &) noexcept;
    Input input_;
    SshService ssh_;
    Settings settings_;
    unsigned text_style_ = 2;
    void cycle_text_size() noexcept;
    void cycle_language() noexcept;
    Research research_;
    bool settings_open_ = false, ssh_enabled_ = false, settings_error_ = false;
    bool password_visible_ = false;
    PasswordEntry password_entry_;
    const char *password_result_ = "";
    void begin_password() noexcept;
    void accept_password() noexcept;
    void password_key(Key, char) noexcept;
    void password_controller(std::uint32_t) noexcept;
    void draw_password(ps5::demo::Canvas &) noexcept;
    std::array<char, 48> address_{};
    std::uint64_t next_address_ = 0;
    void toggle_ssh() noexcept;
    void draw_settings(ps5::demo::Canvas &canvas) noexcept;
    bool about_open_ = false; // The About page, shown in place of Settings.
    void draw_about(ps5::demo::Canvas &canvas) noexcept;
    // Linux environments, shown in place of Settings. State comes from the guest's
    // `ps5term-env list --json`, run (like every change) on a guest agent session.
    bool environments_open_ = false;
    ui::EnvButton environments_focus_ = ui::EnvButton::open_alpine;
    LinuxEnvironments environments_;
    enum class EnvironmentJob
    {
        none,
        list,  // quick: names, states, default, disk
        sizes, // slower: each installed environment's size
        change
    };
    EnvironmentJob environment_job_ = EnvironmentJob::none;
    int environment_handle_ = 0;
    bool environments_refresh_ = false, environment_sizes_known_ = false;
    std::uint64_t environment_retry_at_ = 0, environment_job_deadline_ = 0;
    std::array<char, 2048> environment_output_{};
    std::size_t environment_output_size_ = 0;
    std::array<char, 200> environment_message_{};
    bool environment_problem_ = false;
    ui::EnvButton environment_armed_ = ui::EnvButton::none;
    std::uint64_t environment_armed_until_ = 0;
    unsigned environment_change_ = 0; // EnvButton of the running change
    // The disk's and its backup's sizes, read once a second while the screen is open
    // (drawing must not stat files every frame).
    unsigned storage_disk_mib_ = 0, storage_backup_mib_ = 0;
    std::uint64_t storage_checked_at_ = 0;
    void refresh_storage_sizes() noexcept;
    void open_environments() noexcept;
    void storage_status(char *out, std::size_t size) const noexcept; // Empty when idle.
    void draw_environments(ps5::demo::Canvas &canvas) noexcept;
    void environments_key(Key key, char character) noexcept;
    void environments_controller(std::uint32_t pressed) noexcept;
    void environments_click() noexcept;
    void environment_action(ui::EnvButton button) noexcept;
    void move_environment_focus(int direction) noexcept;
    bool environment_shown(ui::EnvButton button) const noexcept;
    void tick_environments() noexcept;
    bool start_environment_job(EnvironmentJob job, const char *command) noexcept;
    void finish_environment_job(int status) noexcept;
    bool open_environment_tab(LinuxEnvironment environment) noexcept;
    void environment_say(const char *message, bool problem = false) noexcept;
    void update_address() noexcept;
    std::size_t scroll_ = 0;
    bool keyboard_ = false, shift_ = false;
    unsigned selected_ = 0;
    std::uint64_t now_ = 0;
    void submit() noexcept;
    void sync_terminal_size() noexcept;

    // Mouse: pointer, clicks, selection, clipboard and the right-click menu.
    Mouse mouse_;
    ui::Pointer pointer_;
    ui::Visibility pointer_shown_;
    ui::ClickCounter clicks_;
    ui::Selection selection_;
    bool output_selection_ = false;
    ui::TextPos selection_cursor_{};
    void selection_key(Key) noexcept;
    ui::Clipboard clipboard_;
    ui::EventLog log_;
    ui::TextPos drag_anchor_{};
    ui::Rect menu_{};
    std::size_t speed_ = ui::default_speed;
    std::uint32_t mouse_buttons_ = 0, keyboard_mods_ = 0, focus_mouse_buttons_ = 0;
    std::uint64_t drag_scroll_at_ = 0;
    unsigned drag_mode_ = 0; // 0 none, 1 characters, 2 words, 3 lines
    bool menu_open_ = false, mouse_connected_ = false;
    int mouse_status_ = 0;
    void left_press(std::uint64_t now) noexcept;
    void drag() noexcept;
    void wheel(int notches) noexcept;
    unsigned settings_focus_ = 0, password_focus_ = 0, menu_focus_ = 0;
    bool focus_visible_ = false, menu_keyboard_ = false;
    // An environment to open in a new tab once Linux has started (Linux environments screen).
    bool pending_environment_ = false;
    LinuxEnvironment pending_environment_name_ = LinuxEnvironment::base;
    std::uint64_t environment_ready_at_ = 0;
    bool storage_requested_ = false;
    const char *storage_result_ = "";
    bool host_mount_whole_ = false, host_mount_error_ = false;
    void open_settings() noexcept;
    void settings_action(ui::SettingsButton) noexcept;
    bool focused(const ui::Rect &) const noexcept;
    void settings_click() noexcept;
    void menu_action(ui::MenuItem item) noexcept;
    void copy() noexcept;
    void paste() noexcept;
    void select_all() noexcept;
    void clear_screen() noexcept;
    void close_settings() noexcept;
    void set_speed(std::size_t index) noexcept;
    void type_key(std::size_t index) noexcept;
    void terminal_key(TerminalModel::Key key, Key fallback) noexcept;
    void reset_terminal() noexcept;
    bool send_control(char character) noexcept;
    bool mouse_to_program(std::uint32_t pressed, std::uint32_t released, int wheel) noexcept;
    std::size_t view_end() const noexcept;
    std::uint64_t seen_history_end_ = 0, reconnect_armed_until_ = 0;
    ui::SettingsButton armed_button_ = ui::SettingsButton::none;
    std::uint64_t armed_until_ = 0;
    bool was_alternate_ = false, password_loaded_ = false, password_available_ = false;
    std::array<char, 256> password_cache_{};
    std::array<char, 16384> paste_buffer_{};
    void move_selection(std::uint32_t pressed) noexcept;
    unsigned osk_modifiers_ = 0; // One-shot Ctrl/Alt from the on-screen keyboard.
    void unit(ui::TextPos at, unsigned mode, ui::TextPos &first, ui::TextPos &last) const noexcept;
    ui::Grid grid() const noexcept;
    std::size_t max_scroll() const noexcept;
    ui::TextPos text_at(int x, int y) noexcept;
    void draw_terminal(ps5::demo::Canvas &canvas) noexcept;
    void draw_menu(ps5::demo::Canvas &canvas) noexcept;
    void draw_pointer(ps5::demo::Canvas &canvas) noexcept;
    void label_button(ps5::demo::Canvas &canvas, const ui::Rect &rect, std::string_view label,
                      unsigned scale, ps5::demo::Color color) noexcept;
    bool hover(const ui::Rect &rect) const noexcept;
};
extern App app;
} // namespace term
