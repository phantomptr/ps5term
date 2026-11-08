// PS5Term - the app's own words in the console's language. GPL-3.0-or-later.
//
// English is in the source, wrapped in tr("...") where it is drawn (or N_("...") where it is
// only named, for tr() to translate later). tools/generate-i18n.py turns i18n/<code>.json into
// src/i18n_strings.inc, the tables tr() searches. A string without a translation stays English.
// Only languages the bundled font can draw are offered (Latin and Cyrillic scripts).
#pragma once
#include <cstddef>
#include <string_view>

// Marks English for translation where the text is stored, not drawn.
#define N_(text) text

namespace term::i18n
{
// 0 is Auto: the console's own language when PS5Term has it, otherwise English.
inline constexpr unsigned automatic = 0, english = 1;
unsigned count() noexcept; // Auto + every language, the order Settings cycles through
std::string_view name(unsigned language) noexcept; // its own name, e.g. "Deutsch"
std::string_view code(unsigned language) noexcept; // e.g. "de", "pt-BR"

// The choice in Settings (0..count()-1) and the language it resolves to (never Auto).
void select(unsigned language, unsigned system = english) noexcept;
unsigned selected() noexcept;
unsigned current() noexcept;

// The PS5's system language (sceSystemServiceParamGetInt), mapped to ours; English if
// unknown, unsupported or unavailable (host builds).
unsigned system_language() noexcept;
// The language for a PS5 system-language id (SCE_SYSTEM_SERVICE_PARAM_ID_LANG values).
unsigned from_system_id(int id) noexcept;

// English in, the current language out (the same text when there is no translation). The
// result is NUL-terminated and lives as long as the program, so it can be a printf format.
std::string_view tr(std::string_view english) noexcept;
const char *trc(const char *english) noexcept;
// snprintf with a translated format, e.g. format(out, size, trc("Tab %u"), n). Every translation
// keeps the English printf conversions (tools/generate-i18n.py checks), so the arguments fit.
int format(char *out, std::size_t size, const char *translated_format, ...) noexcept;

// Scripts the bundled font cannot draw (CJK, Arabic, Indic, Thai, Vietnamese) come as runs:
// sentences rendered ahead of time (assets/i18n/<code>.runs, made by tools/generate-i18n.py).
// In translated text a run is U+E000 followed by its number in four hex digits.
struct Run
{
    unsigned width = 0, height = 0, baseline = 0; // pixels at the normal text size (10x20 cells)
    const unsigned char *data = nullptr;          // level << 4 | (repeat - 1), row by row
    std::size_t size = 0;
};
inline constexpr std::size_t run_marker_size = 7; // U+E000 (3 bytes) + 4 hex digits
// U+E001 first in a right-to-left translation: drawn with its pieces in reverse order.
inline constexpr std::size_t rtl_marker_size = 3;
// The run starting at text[at], if there is one there.
bool run_at(std::string_view text, std::size_t at, Run &run) noexcept;
// Width in cells at the normal size: characters, with runs at their drawn width.
std::size_t cells(std::string_view text) noexcept;
// For text that does not go through the renderer (terminal output): English when the current
// language is drawn from runs, otherwise the translation.
std::string_view plain(std::string_view english) noexcept;
// Where <code>.runs files are read from: /app0/assets/i18n on the console.
void set_run_directory(const char *directory) noexcept;
} // namespace term::i18n

namespace term
{
using i18n::tr;
using i18n::trc;
} // namespace term
