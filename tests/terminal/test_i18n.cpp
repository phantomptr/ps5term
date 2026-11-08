// PS5Term - app translations: lookup, fallback, console language and formats.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"
#include "text_metrics.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>
using namespace term;

static unsigned find(std::string_view code)
{
    for (unsigned language = i18n::english; language < i18n::count(); ++language)
        if (i18n::code(language) == code)
            return language;
    return 0;
}

int main()
{
    // Auto with an unknown console language is English; English text passes through.
    i18n::select(i18n::automatic, i18n::from_system_id(999));
    assert(i18n::current() == i18n::english);
    assert(tr("Settings") == "Settings");
    assert(tr("never translated text") == "never translated text");
    assert(i18n::name(i18n::automatic) == "Auto" && i18n::code(i18n::automatic) == "auto");

    // Console language ids map to ours; regional variants share a translation.
    assert(i18n::from_system_id(1) == i18n::english);  // English (US)
    assert(i18n::from_system_id(18) == i18n::english); // English (UK)
    if (const unsigned de = find("de"))
    {
        assert(i18n::from_system_id(4) == de);
        i18n::select(i18n::automatic, de); // Auto follows the console.
        assert(i18n::current() == de);
        assert(tr("Settings") != "Settings");                           // Translated...
        assert(tr("never translated text") == "never translated text"); // ...or left English.
        char line[64];
        i18n::format(line, sizeof line, trc("Language: %s"), "Deutsch");
        assert(std::strstr(line, "Deutsch"));
        i18n::select(i18n::english, de); // A choice in Settings beats the console.
        assert(i18n::current() == i18n::english && tr("Settings") == "Settings");
    }
    if (const unsigned fr = find("fr"))
        assert(i18n::from_system_id(2) == fr && i18n::from_system_id(22) == fr);
    if (const unsigned pt = find("pt-BR"))
        assert(i18n::from_system_id(7) == pt && i18n::from_system_id(17) == pt);
    assert(i18n::from_system_id(0) == i18n::english); // Japanese: no font yet.

    // Every language answers every lookup with text the renderer can measure.
    for (unsigned language = i18n::english; language < i18n::count(); ++language)
    {
        i18n::select(language);
        assert(!tr("Settings").empty() && !i18n::name(language).empty());
    }
    // UTF-8 width counts characters, not bytes.
    assert(text_metrics::length("Größe") == 5 && text_metrics::length("Русский") == 7);
    // Out-of-range choices fall back to Auto.
    i18n::select(250);
    assert(i18n::selected() == i18n::automatic);
    std::printf("PASS: translations, fallback, console language, formats (%u languages)\n",
                i18n::count() - 1);
}
