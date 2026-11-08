// PS5Term - PTY-backed native process launcher. GPL-3.0-or-later.
#pragma once
#include "launcher.hpp"
namespace term::platform
{
// Host reference uses posix_openpt and fork/exec. On PS5 this returns an empty
// API unless PS5TERM_NATIVE_PTY_RESEARCH is explicitly compiled in. The launcher
// never changes credentials, capabilities, prison or filesystem roots.
// The context owns up to max_children concurrent children.
// Call pty_launcher_destroy() to clean up when done.
LauncherApi pty_launcher_create() noexcept;
void pty_launcher_destroy(LauncherApi &api) noexcept;
} // namespace term::platform
