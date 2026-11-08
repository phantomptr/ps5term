// PS5Term - actual preference persistence tests. SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
// The native app's access() fails with EPERM even for a flag open() can read.
extern "C" int access(const char *, int)
#ifndef __APPLE__
    noexcept
#endif
{
    errno = EPERM;
    return -1;
}
int main()
{
    char directory[] = "/tmp/ps5term-setting-XXXXXX";
    assert(mkdtemp(directory));
    std::string child = std::string(directory) + "/settings", flag = child + "/ssh.enabled";
    term::Settings settings(child.c_str(), flag.c_str());
    assert(settings.text_style() == 2);
    assert(settings.set_text_style(1));
    assert(settings.text_style() == 1);
    assert(!settings.set_text_style(0));
    assert(!settings.set_text_style(4));
    assert(settings.text_style() == 1);
    assert(!settings.ssh_enabled());
    assert(settings.set_ssh_enabled(true));
    assert(settings.ssh_enabled());
    term::Settings reload(child.c_str(), flag.c_str());
    assert(reload.ssh_enabled());
    assert(reload.set_ssh_enabled(false));
    assert(!settings.ssh_enabled());
    assert(reload.set_ssh_enabled(false));
    term::Settings bad("/does-not-exist/ps5term", "/does-not-exist/ps5term/ssh.enabled");
    assert(!bad.set_ssh_enabled(true));
    assert(!bad.ssh_enabled());
    std::string password_path = child + "/password";
    char password[256]{};
    assert(!term::Settings::read_ssh_password(password, sizeof(password), password_path.c_str()));
    int file = open(password_path.c_str(), O_WRONLY | O_CREAT, 0600);
    assert(file >= 0);
    assert(write(file, "1234 \r\nignored", 14) == 14);
    close(file);
    assert(term::Settings::read_ssh_password(password, sizeof(password), password_path.c_str()));
    assert(std::strcmp(password, "1234") == 0);
    assert(!term::Settings::read_ssh_password(password, 4, password_path.c_str()));
    assert(password[0] == 0);
    unlink(password_path.c_str());
    unlink((child + "/text.size").c_str());
    rmdir(child.c_str());
    rmdir(directory);
}
