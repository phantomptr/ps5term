// PS5Term - the console's system language. GPL-3.0-or-later.
#include "../i18n.hpp"

#if defined(__PROSPERO__)
extern "C" int sceSystemServiceParamGetInt(int id, int *value);
#endif

namespace term::i18n
{
unsigned system_language() noexcept
{
#if defined(__PROSPERO__)
    constexpr int language_parameter = 1; // SCE_SYSTEM_SERVICE_PARAM_ID_LANG
    int value = -1;
    if (sceSystemServiceParamGetInt(language_parameter, &value) == 0)
        return from_system_id(value);
#endif
    return english;
}
} // namespace term::i18n
