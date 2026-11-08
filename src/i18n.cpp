// PS5Term - translation lookup. GPL-3.0-or-later.
#include "i18n.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <new>
#include <unistd.h>

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
    bool rendered; // drawn from assets/i18n/<code>.runs
};
#include "i18n_strings.inc" // tables[]: English first (no entries), then each translation

unsigned chosen = automatic, resolved = english;

#if defined(__PROSPERO__)
const char *run_directory = "/app0/assets/i18n";
#else
const char *run_directory = "assets/i18n";
#endif
// The current language's runs file, read whole when the language is chosen.
unsigned char *runs = nullptr;
std::size_t runs_size = 0, run_count = 0;
unsigned run_height = 0, run_baseline = 0;

unsigned read16(const unsigned char *at)
{
    return static_cast<unsigned>(at[0]) | static_cast<unsigned>(at[1]) << 8;
}
std::uint32_t read32(const unsigned char *at)
{
    return static_cast<std::uint32_t>(at[0]) | static_cast<std::uint32_t>(at[1]) << 8 |
           static_cast<std::uint32_t>(at[2]) << 16 | static_cast<std::uint32_t>(at[3]) << 24;
}
void drop_runs()
{
    delete[] runs;
    runs = nullptr;
    runs_size = run_count = 0;
}
// Reads and checks <code>.runs; false leaves no runs loaded.
bool load_runs(std::string_view language_code)
{
    drop_runs();
    char path[256];
    if (std::snprintf(path, sizeof path, "%s/%.*s.runs", run_directory,
                      static_cast<int>(language_code.size()),
                      language_code.data()) >= static_cast<int>(sizeof path))
        return false;
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const off_t end = ::lseek(fd, 0, SEEK_END);
    if (end < 12 || end > (16 << 20) || ::lseek(fd, 0, SEEK_SET) != 0)
    {
        ::close(fd);
        return false;
    }
    const auto length = static_cast<std::size_t>(end);
    runs = new (std::nothrow) unsigned char[length];
    std::size_t got = 0;
    while (runs && got < length)
    {
        const auto n = ::read(fd, runs + got, length - got);
        if (n <= 0)
            break;
        got += static_cast<std::size_t>(n);
    }
    ::close(fd);
    if (!runs || got != length || std::memcmp(runs, "P5TR", 4) != 0 || read16(runs + 4) != 1)
    {
        drop_runs();
        return false;
    }
    runs_size = got;
    run_count = read16(runs + 6);
    run_height = read16(runs + 8);
    run_baseline = read16(runs + 10);
    const std::size_t data = 12 + run_count * 12;
    if (data > runs_size || run_height == 0 || run_height > 64)
    {
        drop_runs();
        return false;
    }
    for (std::size_t i = 0; i < run_count; ++i) // Every run must lie inside the file.
    {
        const auto *entry = runs + 12 + i * 12;
        if (read16(entry) == 0 || data + read32(entry + 4) + read32(entry + 8) > runs_size)
        {
            drop_runs();
            return false;
        }
    }
    return true;
}
int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
bool rendered(unsigned language)
{
    return language > english && language < static_cast<unsigned>(std::size(tables)) + 1 &&
           tables[language - 1].rendered;
}
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
    if (chosen != automatic)
        resolved = chosen;
    else
        resolved = system >= english && system < count() ? system : english;
    drop_runs();
    // A rendered language without its runs file would draw markers: use English instead.
    if (rendered(resolved) && !load_runs(code(resolved)))
        resolved = english;
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
    case 0:
        want = "ja";
        break;
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
    case 9:
        want = "ko";
        break;
    case 10:
        want = "zh-TW";
        break;
    case 11:
        want = "zh-CN";
        break;
    case 16:
        want = "pl";
        break;
    case 19:
        want = "tr";
        break;
    case 21:
        want = "ar";
        break;
    case 24:
        want = "hu";
        break;
    case 27:
        want = "th";
        break;
    case 28:
        want = "vi";
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
bool run_at(std::string_view text, std::size_t at, Run &run) noexcept
{
    if (at + run_marker_size > text.size() || static_cast<unsigned char>(text[at]) != 0xEE ||
        static_cast<unsigned char>(text[at + 1]) != 0x80 ||
        static_cast<unsigned char>(text[at + 2]) != 0x80)
        return false;
    unsigned id = 0;
    for (std::size_t k = 3; k < run_marker_size; ++k)
    {
        const int digit = hex(text[at + k]);
        if (digit < 0)
            return false;
        id = id * 16 + static_cast<unsigned>(digit);
    }
    if (!runs || id >= run_count)
        return false;
    const auto *entry = runs + 12 + static_cast<std::size_t>(id) * 12;
    run.width = read16(entry);
    run.height = run_height;
    run.baseline = run_baseline;
    run.data = runs + 12 + run_count * 12 + read32(entry + 4);
    run.size = read32(entry + 8);
    return true;
}
std::size_t cells(std::string_view text) noexcept
{
    std::size_t total = 0;
    for (std::size_t i = 0; i < text.size();)
    {
        Run run;
        if (run_at(text, i, run))
        {
            total += (run.width + 9) / 10;
            i += run_marker_size;
            continue;
        }
        if (i + rtl_marker_size <= text.size() && static_cast<unsigned char>(text[i]) == 0xEE &&
            static_cast<unsigned char>(text[i + 1]) == 0x80 &&
            static_cast<unsigned char>(text[i + 2]) == 0x81)
        {
            i += rtl_marker_size; // direction, not a character
            continue;
        }
        total += (static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U;
        ++i;
    }
    return total;
}
std::string_view plain(std::string_view english_text) noexcept
{
    return rendered(resolved) ? english_text : tr(english_text);
}
void set_run_directory(const char *directory) noexcept
{
    run_directory = directory;
}
} // namespace term::i18n
