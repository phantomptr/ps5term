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
} // namespace term::i18n

namespace term
{
using i18n::tr;
using i18n::trc;
} // namespace term
