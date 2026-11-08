// GPL-3.0-or-later.
#include "password_entry.hpp"
#include <cassert>
#include <string>
int main()
{
    term::PasswordEntry entry;
    entry.begin();
    for (char c : std::string("short"))
        entry.insert(c);
    assert(!entry.advance() && !entry.confirming());
    entry.cancel();
    entry.begin();
    for (char c : std::string("custom pass"))
        entry.insert(c);
    assert(!entry.advance() && entry.confirming());
    entry.insert('x');
    assert(!entry.advance());
    assert(entry.confirming() && entry.size() == 0);
    for (char c : std::string("custom pass"))
        entry.insert(c);
    assert(entry.advance() && entry.value() == "custom pass");
    entry.cancel();
    assert(!entry.active() && entry.value().empty() && entry.size() == 0);
    entry.begin();
    for (unsigned i = 0; i < 70; ++i)
        entry.insert('a');
    assert(entry.size() == 64);
    entry.insert('\n');
    assert(entry.size() == 64);
    entry.home();
    entry.erase();
    assert(entry.size() == 63);
    entry.insert('b');
    assert(entry.cursor() == 1);
    entry.backspace();
    assert(entry.size() == 63);
    entry.end();
    entry.left();
    entry.right();
    assert(entry.cursor() == 63);
}
