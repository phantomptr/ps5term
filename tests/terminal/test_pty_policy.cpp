// SPDX-License-Identifier: GPL-3.0-or-later
// Normal PS5 startup must not attempt privileged process creation.
#include "platform/pty_launcher.hpp"
#include <cassert>
extern "C" int ps5term_escalate()
{
    assert(false);
    return -1;
}
struct termios;
struct winsize;
extern "C" int ps5term_forkpty(int *, const termios *, const winsize *)
{
    assert(false);
    return -1;
}
int main()
{
    auto api = term::platform::pty_launcher_create();
    assert(!api.context);
    term::platform::pty_launcher_destroy(api);
}
