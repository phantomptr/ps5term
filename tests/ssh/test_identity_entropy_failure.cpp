// GPL-3.0-or-later. A failed entropy source must not create identity files.
#include "backend/owned_ssh/identity.hpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <unistd.h>
extern "C" int ps5term_host_seed(unsigned char *, unsigned int)
{
    return -1;
}
int main()
{
    char path[] = "/tmp/ps5term-noentropy-XXXXXX";
    assert(mkdtemp(path));
    const std::string directory = std::string(path) + "/ssh";
    term::ssh::Identity identity;
    assert(!identity.load_or_create(directory.c_str()));
    assert(identity.key().empty() && identity.password().empty());
    assert(access((directory + "/host-key.der").c_str(), F_OK) != 0);
    assert(access((directory + "/password").c_str(), F_OK) != 0);
    assert(rmdir(directory.c_str()) == 0 && rmdir(path) == 0);
}
