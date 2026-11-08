/* PS5Term - no-fallback launch policy. GPL-3.0-or-later. */
#define PS5TERM_LAUNCH_POLICY_ONLY
#include "tools/dev/native_launch_probe.c"
#include <assert.h>
#include <stddef.h>
static int calls, response;
static int launch(const char *title, const char *const *args, const void *raw)
{
    const struct launch_context *context = raw;
    assert(title && args && args[0] == NULL);
    assert(context->size == 32 && context->user == 123);
    ++calls;
    return response;
}
int main(void)
{
    assert(ps5term_launch_once(NULL, 123) < 0 && calls == 0);
    assert(ps5term_launch_once(launch, -1) < 0 && calls == 0);
    assert(ps5term_launch_once(launch, 0) < 0 && calls == 0);
    response = (int)0x80aa001a;
    assert(ps5term_launch_once(launch, 123) == response && calls == 1);
    response = 0xe018;
    assert(ps5term_launch_once(launch, 123) == response && calls == 2);
}
