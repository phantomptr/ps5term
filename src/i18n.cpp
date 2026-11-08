// PS5Term - translation lookup. GPL-3.0-or-later.
#include "i18n.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace term::i18n
{
namespace
{
struct Entry
{
    const char *english, *text;
};
struct Table
{
    const char *code, *name;
    const Entry *entries;
    std::size_t size;
};
#include "i18n_strings.inc" // tables[]: English first (no entries), then each translation

unsigned chosen = automatic, resolved = english;
} // namespace

unsigned count() noexcept
{
    return static_cast<unsigned>(std::size(tables)) + 1;
}
std::string_view name(unsigned language) noexcept
{
    if (language == automatic || language >= count())
        return "Auto";
    return tables[language - 1].name;
}
std::string_view code(unsigned language) noexcept
{
    if (language == automatic || language >= count())
        return "auto";
    return tables[language - 1].code;
}
void select(unsigned language, unsigned system) noexcept
{
    chosen = language < count() ? language : automatic;
    resolved = chosen != automatic                     ? chosen
               : system >= english && system < count() ? system
                                                       : english;
}
unsigned selected() noexcept
{
    return chosen;
}
unsigned current() noexcept
{
    return resolved;
}
unsigned from_system_id(int id) noexcept
{
    // SCE_SYSTEM_SERVICE_PARAM_ID_LANG values; regional variants share one translation.
    std::string_view want;
    switch (id)
    {
    case 2:  // French
    case 22: // French (Canada)
        want = "fr";
        break;
    case 3:  // Spanish
    case 20: // Spanish (Latin America)
        want = "es";
        break;
    case 4:
        want = "de";
        break;
    case 5:
        want = "it";
        break;
    case 7:  // Portuguese (Portugal)
    case 17: // Portuguese (Brazil)
        want = "pt-BR";
        break;
    case 8:
        want = "ru";
        break;
    case 16:
        want = "pl";
        break;
    case 19:
        want = "tr";
        break;
    case 24:
        want = "hu";
        break;
    case 29:
        want = "id";
        break;
    default:
        return english;
    }
    for (unsigned language = english; language < count(); ++language)
        if (code(language) == want)
            return language;
    return english;
}
std::string_view tr(std::string_view english_text) noexcept
{
    if (resolved <= english || resolved >= count())
        return english_text;
    const auto &table = tables[resolved - 1];
    const auto *first = table.entries, *last = table.entries + table.size;
    const auto *at =
        std::lower_bound(first, last, english_text, [](const Entry &entry, std::string_view key)
                         { return std::string_view(entry.english) < key; });
    return at != last && std::string_view(at->english) == english_text ? std::string_view(at->text)
                                                                       : english_text;
}
const char *trc(const char *english_text) noexcept
{
    return tr(english_text).data(); // Table text and literals are both NUL-terminated.
}
int format(char *out, std::size_t size, const char *translated_format, ...) noexcept
{
    std::va_list arguments;
    va_start(arguments, translated_format);
    const int written = std::vsnprintf(out, size, translated_format, arguments);
    va_end(arguments);
    return written;
}
} // namespace term::i18n
