// PS5Term - GPL-3.0-or-later.
#include "terminal_model.hpp"
#include <array>
#include <cassert>
#include <string>
using term::TerminalModel;
static std::string output(TerminalModel &model)
{
    std::string result;
    std::array<char, 4096> bytes{};
    while (auto n = model.take_output(bytes))
        result.append(bytes.data(), n);
    return result;
}
int main()
{
    // REP ending one column before the margin must not arm the pending wrap: ncurses draws
    // a box's bottom border as m, q, REP, then the corner with autowrap off (local libvterm
    // patch; upstream 0.3.3 wrapped the corner and scrolled the whole screen).
    {
        term::TerminalModel box(4, 10);
        box.feed("\033(0\033[1;1Hlq\033[7bk\033[4;1Hmq\033[7b\033[?7lj\033[?7h");
        assert(box.cell(0, 0).codepoints[0] == 0x250c && box.cell(0, 9).codepoints[0] == 0x2510);
        assert(box.cell(3, 0).codepoints[0] == 0x2514 && box.cell(3, 8).codepoints[0] == 0x2500);
        assert(box.cell(3, 9).codepoints[0] == 0x2518);
        // A repeat that does reach the margin still wraps the next character, as text does.
        term::TerminalModel wrap(2, 10);
        wrap.feed("ab\033[8bc");
        assert(wrap.cell(0, 9).codepoints[0] == 'b' && wrap.cell(1, 0).codepoints[0] == 'c');
    }
    // Black as a background is the panel's colour; black text keeps its readable grey.
    {
        term::TerminalModel colours(1, 10);
        colours.feed("\033[37;40mA\033[41mB\033[30;49mC");
        assert(colours.cell(0, 0).default_background && !colours.cell(0, 1).default_background);
        assert(colours.cell(0, 2).foreground.red == 72 && colours.cell(0, 2).default_background);
    }
    // Allocation refusal must leave an inert model rather than enter upstream
    // libvterm with a null pointer. Resize/feed faults retain queued input.
    // Sweep several construction cutoffs to cover partially built graphs.
    for (std::size_t limit = 0; limit < 32 * 1024; limit += 257)
    {
        TerminalModel partial(4, 12, limit);
        if (partial.healthy())
            assert(partial.feed("ready") == 5);
        else
            assert(partial.rows() == 0 && !partial.key(TerminalModel::Key::enter));
    }
    TerminalModel no_memory(4, 12, 0);
    assert(!no_memory.healthy() && no_memory.rows() == 0 && no_memory.feed("x") == 0);
    TerminalModel limited_resize(2, 10, 64 * 1024);
    assert(limited_resize.healthy() && limited_resize.text('q'));
    assert(!limited_resize.resize(120, 240) && !limited_resize.healthy());
    assert(limited_resize.rows() == 0 && limited_resize.cell(0, 0).width == 0);
    assert(output(limited_resize) == "q");
    TerminalModel limited_feed(2, 10, 64 * 1024);
    assert(limited_feed.healthy());
    std::string combining = "a";
    for (int i = 0; i < 30000; ++i)
        combining += "\xcc\x81";
    assert(limited_feed.feed(combining) == 0 && !limited_feed.healthy());
    assert(!limited_feed.resize(2, 10) && !limited_feed.text('x'));
    TerminalModel recovered(2, 10);
    assert(recovered.healthy() && recovered.feed("ok") == 2);
    TerminalModel history(2, 8);
    history.feed("\x1b[38;2;12;34;56mA\r\nB\r\nC");
    assert(history.history_size() == 1 && history.history_first() == 1 &&
           history.history_end() == 2);
    assert(history.history_columns(1) == 8 && history.history_cell(1, 0).codepoints[0] == 'A');
    assert((history.history_cell(1, 0).foreground == TerminalModel::Rgb{12, 34, 56}));
    history.feed("\x1b[?1049hX\r\nY\r\nZ");
    assert(history.history_size() == 1 && history.history_end() == 2);
    history.feed("\x1b[?1049l");
    assert(history.resize(4, 5) && history.history_columns(1) == 8);
    history.feed("\x1b[3J");
    assert(history.history_size() == 0 && history.history_first() == 2 &&
           history.history_end() == 2);
    assert(history.history_columns(1) == 0 && history.history_cell(1, 0).width == 0);
    history.feed("\x1b[H1\r\n2\r\n3\r\n4\r\n5");
    assert(history.history_first() == 2 && history.history_end() == 3);
    history.clear_history();
    assert(history.history_size() == 0 && history.history_end() == 3);
    TerminalModel model(4, 12), fragmented(4, 12);
    assert(model.healthy());
    std::array<TerminalModel::Damage, 120> dirty{};
    assert(model.take_damage(dirty) == 4);
    assert(dirty[0].row == 0 && dirty[0].start_column == 0 && dirty[0].end_column == 12);
    assert(model.take_damage(dirty) == 0);
    assert(model.feed("X") == 1);
    assert(model.take_damage(dirty) == 1 && dirty[0].row == 0);
    model.feed("\x1b[3;4H");
    assert(model.take_damage(dirty) == 2); // Old and new cursor positions.
    model.feed("\x1b[2J\x1b[H");
    assert(model.take_damage(std::span<TerminalModel::Damage>(dirty.data(), 1)) == 1);
    assert(model.take_damage(dirty) == 3); // Small consumers retain remaining damage.

    const std::string transcript = "A\xc3\xa9\xe7\x95\x8c"
                                   "e\xcc\x81\x1b[3;2H\x1b[1;3;4;38;2;12;34;56mZ";
    assert(model.feed(transcript) == transcript.size());
    for (char byte : transcript)
        assert(fragmented.feed({&byte, 1}) == 1);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 12; ++col)
            assert(model.cell(row, col) == fragmented.cell(row, col));
    assert(model.cell(0, 1).codepoints[0] == 0xe9);
    assert(model.cell(0, 2).codepoints[0] == 0x754c && model.cell(0, 2).width == 2);
    assert(model.cell(0, 4).codepoints[0] == 'e' && model.cell(0, 4).codepoints[1] == 0x301);
    auto z = model.cell(2, 1);
    assert(z.codepoints[0] == 'Z' && z.bold && z.italic && z.underline == 1);
    assert((z.foreground == TerminalModel::Rgb{12, 34, 56}));
    assert(model.cursor().row == 2 && model.cursor().column == 2);
    model.feed("\x1b[?25l");
    assert(!model.cursor().visible);
    model.feed("\x1b[?25h");
    assert(model.cursor().visible);
    model.feed("\x1b[?1049h\x1b[Hother");
    assert(model.alternate_screen() && model.cell(0, 0).codepoints[0] == 'o');
    model.feed("\x1b[?1049l");
    assert(!model.alternate_screen() && model.cell(0, 0).codepoints[0] == 'A');
    assert(model.resize(6, 20) && model.rows() == 6 && model.columns() == 20);
    assert(model.take_damage(dirty) == 6);
    assert(dirty[5].end_column == 20);
    model.feed("\x1b[2J"); // Undrained damage from the old, wider dimensions.
    assert(model.resize(3, 8));
    assert(model.take_damage(dirty) == 3 && dirty[2].end_column == 8);
    assert(model.resize(6, 20));
    assert(!model.resize(0, 20) && !model.resize(6, 241) && model.columns() == 20);
    assert(model.cell(-1, 0).width == 0 && model.cell(6, 0).width == 0);
    model.feed("\x1b[2;3H\x1b[6n");
    assert(output(model) == "\x1b[2;3R");
    assert(model.key(TerminalModel::Key::up));
    assert(output(model) == "\x1b[A");
    model.feed("\x1b[?1h");
    assert(model.key(TerminalModel::Key::up));
    assert(output(model) == "\x1bOA");
    assert(model.function_key(1));
    assert(output(model) == "\x1bOP");
    assert(model.function_key(12));
    assert(output(model) == "\x1b[24~");
    assert(model.function_key(5, TerminalModel::control));
    assert(output(model) == "\x1b[15;5~");
    assert(!model.function_key(0) && !model.function_key(13) && !model.function_key(1, 8));
    assert(output(model).empty());
    assert(model.text('c', TerminalModel::control));
    assert(output(model) == std::string(1, 3));
    assert(!model.text(0x110000) && !model.text(0xd800));
    model.feed("\x1b[?2004h");
    assert(model.paste("hello\nworld"));
    assert(output(model) == "\x1b[200~hello\nworld\x1b[201~");
    assert(!model.paste(std::string(TerminalModel::output_capacity, 'x')) && model.healthy());
    TerminalModel invalid(121, 10);
    assert(!invalid.healthy() && invalid.feed("x") == 0);
    TerminalModel budget(2, 10);
    assert(budget.feed(std::string(70000, 'x')) == TerminalModel::feed_budget);
    // Mouse output is opt-in by the terminal program; ordinary app selection
    // remains possible until a frontend explicitly routes these events.
    TerminalModel mouse(4, 12);
    assert(mouse.mouse_reporting() == TerminalModel::MouseReporting::none);
    assert(!mouse.mouse_move(1, 2) && !mouse.mouse_button(1, true));
    mouse.feed("\x1b[?1000h\x1b[?1006h");
    assert(mouse.mouse_reporting() == TerminalModel::MouseReporting::click);
    assert(mouse.mouse_move(1, 2) && output(mouse).empty());
    assert(mouse.mouse_button(1, true));
    assert(output(mouse) == "\x1b[<0;3;2M");
    assert(mouse.mouse_button(1, false));
    assert(output(mouse) == "\x1b[<0;3;2m");
    assert(mouse.mouse_button(4, true));
    assert(output(mouse) == "\x1b[<64;3;2M");
    assert(!mouse.mouse_button(4, false) && !mouse.mouse_button(0, true));
    assert(!mouse.mouse_move(-1, 0) && !mouse.mouse_move(4, 0) && !mouse.mouse_move(0, 12));
    assert(!mouse.mouse_button(1, true, 8) && output(mouse).empty());
    mouse.feed("\x1b[?1002h");
    assert(mouse.mouse_reporting() == TerminalModel::MouseReporting::drag);
    assert(mouse.mouse_button(1, true));
    output(mouse);
    assert(mouse.mouse_move(2, 3, TerminalModel::shift));
    assert(output(mouse) == "\x1b[<36;4;3M");
    assert(mouse.mouse_button(1, false));
    output(mouse);
    mouse.feed("\x1b[?1003h");
    assert(mouse.mouse_reporting() == TerminalModel::MouseReporting::move);
    assert(mouse.mouse_move(0, 0));
    assert(output(mouse) == "\x1b[<35;1;1M");
    mouse.feed("\x1b[?1003l");
    assert(mouse.mouse_reporting() == TerminalModel::MouseReporting::none);
    assert(!mouse.mouse_move(1, 1) && output(mouse).empty());
    assert(mouse.focus(true) && output(mouse).empty());
    mouse.feed("\x1b[?1004h");
    assert(mouse.focus(false));
    assert(output(mouse) == "\x1b[O");
    assert(mouse.focus(true));
    assert(output(mouse) == "\x1b[I");
    // Input queue saturation is backpressure, with every accepted byte retained.
    std::size_t accepted = 0;
    while (budget.text('x'))
        ++accepted;
    assert(accepted > 0 && accepted <= TerminalModel::output_capacity && budget.healthy());
    assert(output(budget) == std::string(accepted, 'x'));
    assert(budget.text('y') && output(budget) == "y");
    // Wrap both sides of the ring while retaining byte-for-byte ordering.
    const std::string chunk(10000, 'z');
    for (int i = 0; i < 100; ++i)
    {
        assert(budget.paste(chunk));
        assert(output(budget) == chunk);
    }
    // Unsolicited terminal replies cannot grow the output queue without bound.
    std::string queries;
    for (int i = 0; i < 1000; ++i)
        queries += "\x1b[6n";
    for (int i = 0; i < 100 && budget.healthy(); ++i)
        budget.feed(queries);
    assert(!budget.healthy() && budget.feed("x") == 0);
    assert(output(budget).size() <= TerminalModel::output_capacity);
}
