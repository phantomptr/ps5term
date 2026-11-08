// PS5Term - virtual input research monitor. GPL-3.0-or-later.
//
// Research build only (APP_DEFINITIONS=PS5TERM_RESEARCH): at start PS5Term
// opens the initial user's mouse through a directly linked libSceMouse and,
// for five minutes, logs every mouse sample and every keyboard change (from
// the app's own keyboard handle) with CLOCK_MONOTONIC microseconds, on screen
// and to /data/ps5term/vdev-monitor.txt. "vdev monitor [seconds]" and
// "vdev monitor off" are accepted typed or, one per line, in
// /data/ps5term/research.cmd (polled once a second, removed when read).
// The normal build compiles stubs and imports nothing extra.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace term
{
class TerminalModel;
class Research
{
  public:
    // Queue a typed command ("vdev ..."); runs on the next tick.
    bool queue(std::string_view command) noexcept
    {
        if (command.size() < 5 || command.substr(0, 5) != "vdev ")
            return false;
        std::size_t n = command.size() < pending_.size() - 1 ? command.size() : pending_.size() - 1;
        for (std::size_t i = 0; i < n; ++i)
            pending_[i] = command[i];
        pending_[n] = 0;
        return true;
    }
    void tick(TerminalModel &model, std::uint64_t now) noexcept;
    // Raw keyboard samples, forwarded by Input while the monitor runs.
    void keyboard(std::uint32_t modifiers, const std::uint16_t *keys, int count, bool connected,
                  bool intercepted) noexcept;

  private:
    std::array<char, 128> pending_{};
    std::uint64_t next_poll_ = 0, monitor_until_ = 0, next_open_ = 0;
    bool started_ = false, monitor_ = false, mouse_loaded_ = false;
    int mouse_ = -1, user_ = -1, last_open_rc_ = 0, fd_ = -1;
    std::uint32_t mouse_buttons_ = 0, kb_modifiers_ = 0;
    std::uint8_t mouse_connected_ = 0;
    bool kb_connected_ = false;
    std::array<std::uint16_t, 16> kb_keys_{};
    unsigned raw_dumps_ = 0, mouse_events_ = 0, kb_events_ = 0;
    TerminalModel *model_ = nullptr;
    void run(std::string_view command, std::uint64_t now) noexcept;
    void start_monitor(std::uint64_t now, unsigned seconds) noexcept;
    void stop_monitor(const char *why) noexcept;
    void open_mouse(std::uint64_t now) noexcept;
    void poll_mouse() noexcept;
    void log(const char *text) noexcept;
};
} // namespace term
