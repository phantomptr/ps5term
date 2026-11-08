// PS5Term - portable VT cell model. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace term
{
class TerminalModel
{
  public:
    static constexpr std::size_t output_capacity = 256 * 1024, feed_budget = 64 * 1024,
                                 max_engine_memory = 16 * 1024 * 1024;
    static constexpr int max_rows = 120, max_columns = 240;
    enum class Key
    {
        enter,
        tab,
        backspace,
        escape,
        up,
        down,
        left,
        right,
        insert,
        erase,
        home,
        end,
        page_up,
        page_down
    };
    enum class MouseReporting
    {
        none,
        click,
        drag,
        move
    };
    enum Modifier : unsigned
    {
        none = 0,
        shift = 1,
        alt = 2,
        control = 4
    };
    struct Rgb
    {
        std::uint8_t red = 0, green = 0, blue = 0;
        bool operator==(const Rgb &) const = default;
    };
    struct Cell
    {
        std::array<std::uint32_t, 6> codepoints{};
        Rgb foreground{}, background{};
        bool default_background = true; // Unset, so the panel shows through; explicit black paints.
        unsigned width = 0, underline = 0;
        bool bold = false, italic = false, blink = false, reverse = false, conceal = false,
             strike = false;
        bool operator==(const Cell &) const = default;
    };
    struct Damage
    {
        int row = 0, start_column = 0, end_column = 0;
    };
    struct Cursor
    {
        int row = 0, column = 0;
        bool visible = true;
    };
    // Engine heap budget includes allocation headers; output queue is separate.
    // A memory fault makes the model inert; recreate it to start a new session.
    TerminalModel(int rows, int columns, std::size_t memory_limit = max_engine_memory) noexcept;
    ~TerminalModel();
    TerminalModel(const TerminalModel &) = delete;
    TerminalModel &operator=(const TerminalModel &) = delete;
    bool healthy() const noexcept;
    int rows() const noexcept;
    int columns() const noexcept;
    // Caller retains the unconsumed suffix. A fault stops further processing.
    std::size_t feed(std::string_view bytes) noexcept;
    Cell cell(int row, int column) const noexcept;
    Cursor cursor() const noexcept;
    // End column is exclusive. Unreturned rows remain dirty for the next call.
    std::size_t take_damage(std::span<Damage> destination) noexcept;
    bool alternate_screen() const noexcept;
    // Immutable physical-line snapshots, retaining original width across resize.
    // IDs survive eviction/clear without reuse; missing IDs yield empty cells.
    std::size_t history_size() const noexcept;
    std::uint64_t history_first() const noexcept;
    std::uint64_t history_end() const noexcept;
    std::size_t history_columns(std::uint64_t id) const noexcept;
    Cell history_cell(std::uint64_t id, std::size_t column) const noexcept;
    void clear_history() noexcept;
    bool resize(int rows, int columns) noexcept;
    // False on invalid input or queue backpressure; no bytes queued in that case.
    bool key(Key key, unsigned modifiers = none) noexcept;
    bool function_key(unsigned number, unsigned modifiers = none) noexcept;
    bool text(std::uint32_t codepoint, unsigned modifiers = none) noexcept;
    bool paste(std::string_view bytes) noexcept;
    // Exact bytes for the program, in order with queued keys, such as control codes.
    bool raw(std::string_view bytes) noexcept;
    MouseReporting mouse_reporting() const noexcept;
    // Coordinates are terminal cells. Buttons 1..3 are held, 4/5 are wheel
    // press events. Frontend selection override decides when to route these.
    bool mouse_move(int row, int column, unsigned modifiers = none) noexcept;
    bool mouse_button(unsigned button, bool pressed, unsigned modifiers = none) noexcept;
    bool focus(bool focused) noexcept;
    void set_default_colors(Rgb foreground, Rgb background) noexcept;
    std::size_t take_output(std::span<char> bytes) noexcept;

  private:
    struct Impl;
    Impl *impl_ = nullptr;
};
} // namespace term
