// SPDX-License-Identifier: GPL-3.0-or-later
#include "terminal.hpp"
#include "telnet.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace term;
int main()
{
    Screen s;
    s.feed("abc\rZ\nnext");
    assert(s.line(0) == "Zbc");
    assert(s.line(1) == "next");
    s.feed("\x1b[31");
    s.feed("m red\x1b[0m");
    assert(s.line(1) == "next red");
    s.feed("\r\x1b[Kok");
    assert(s.line(1) == "ok");
    s.clear();
    s.feed(std::string(Screen::columns + 1, 'x'));
    assert(s.line(0).size() == Screen::columns);
    assert(s.line(1) == "x");
    s.clear();
    for (int i = 0; i < 700; i++)
        s.feed("row\n");
    assert(s.count() == Screen::capacity);
    assert(s.line(s.count() - 2) == "row");
    Editor e;
    e.insert("ls /data");
    e.left();
    e.backspace();
    assert(e.text() == "ls /daa");
    e.home();
    e.insert("X");
    e.erase();
    assert(e.text() == "Xs /daa");
    e.commit();
    e.insert("draft");
    e.history(-1);
    assert(e.text() == "Xs /daa");
    e.history(1);
    assert(e.text() == "draft");
    e.clear();
    e.insert(std::string(2000, 'a'));
    assert(e.text().size() == Editor::limit);
    Telnet t;
    assert(t.feed("hello\xff", 6) == "hello");
    const char will[] = {char(251), char(1), '!'};
    assert(t.feed(will, 3) == "!");
    assert(t.reply() == std::string("\xff\xfe\x01", 3));
    const char sb[] = {char(255), char(250), char(24), 'a', char(255)};
    assert(t.feed(sb, 5).empty());
    const char se[] = {char(240), 'b', char(255), char(255)};
    assert(t.feed(se, 4) == std::string("b\xff", 2));
    assert(Telnet::encode(std::string("a\xff", 2)).view() == std::string("a\xff\xff", 3));
    std::cout << "Terminal, editor and fragmented Telnet checks passed\n";
}
