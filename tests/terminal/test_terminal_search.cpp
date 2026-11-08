// PS5Term GPL-3.0-or-later.
#include "terminal_search.hpp"
#include <cassert>
#include <cstdio>
#include <string>
int main()
{
    term::SearchQuery query;
    assert(query.insert("é界a"));
    query.left();
    query.backspace();
    assert(query.text() == "éa" && query.cursor() == 2);
    query.home();
    query.erase();
    assert(query.text() == "a");
    query.end();
    assert(query.insert("😀"));
    query.backspace();
    assert(query.text() == "a");
    assert(!query.insert("x\ny") && !query.insert("\xc0\x80") && !query.insert("\xed\xa0\x80"));
    assert(query.text() == "a");
    query.clear();
    std::string full(term::SearchQuery::limit, 'x');
    assert(query.insert(full) && !query.insert("y"));
    term::TerminalModel model(3, 40);
    model.feed("\033[20hbanana é界\nbanana\nlast");
    term::TerminalSearch search;
    assert(search.start(model, "ana"));
    assert(search.busy());
    search.step(model, 1);
    assert(search.busy()); // one cell, not a whole-screen scan
    while (search.busy())
        search.step(model, 7);
    assert(search.count() == 4); // overlapping matches, two rows
    assert(search.match(0).first == 1 && search.match(1).first == 3);
    assert(search.start(model, "é界"));
    while (search.busy())
        search.step(model);
    assert(search.count() == 1 && search.match(0).first == 7 && search.match(0).last == 9);
    model.feed("\nbanana\nbanana\nbanana");
    assert(search.start(model, "banana"));
    while (search.busy())
        search.step(model);
    assert(search.count() == 5 && search.match(0).line < model.history_end());
    assert(search.start(model, "missing"));
    while (search.busy())
        search.step(model);
    assert(search.count() == 0);
    assert(!search.start(model, std::string_view("x", 0)) && !search.busy());
    // Match list saturation is explicit and bounded.
    term::TerminalModel many(20, 240);
    for (int i = 0; i < 3000; ++i)
        many.feed("x");
    assert(search.start(many, "x"));
    while (search.busy())
        search.step(many);
    assert(search.count() == term::TerminalSearch::capacity && search.truncated());
    puts("PASS: bounded incremental search, overlapping UTF-8 matches, history and result cap");
}
