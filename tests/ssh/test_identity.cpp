// GPL-3.0-or-later. Identity lifecycle; temporary files only.
#include "backend/owned_ssh/identity.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <algorithm>
int main()
{
    char path[] = "/tmp/ps5term-identity-XXXXXX";
    assert(mkdtemp(path));
    const auto directory = std::string(path) + "/ssh";
    term::ssh::Identity first;
    assert(first.load_or_create(directory.c_str()));
    assert(first.password().size() >= 24 && !first.key().empty());
    const std::string password(first.password());
    const std::vector<unsigned char> key(first.key().begin(), first.key().end());
    term::ssh::Identity second;
    assert(second.load_or_create(directory.c_str()));
    assert(second.password() == password &&
           std::equal(second.key().begin(), second.key().end(), key.begin(), key.end()));
    // Password replacement persists without rotating the host identity.
    assert(!first.update_password("short"));
    assert(!first.update_password(std::string(65, 'x')));
    assert(!first.update_password("bad\npassword"));
    assert(first.password() == password);
    assert(first.update_password("  custom passphrase  "));
    assert(second.load_or_create(directory.c_str()));
    assert(second.password() == "  custom passphrase  ");
    assert(std::equal(second.key().begin(), second.key().end(), key.begin(), key.end()));
    // A stale view must not overwrite a password changed elsewhere.
    assert(second.update_password("new passphrase"));
    assert(!first.update_password("stale replacement"));
    assert(first.load_or_create(directory.c_str()));
    const auto temporary = directory + "/password.update";
    assert(symlink("password", temporary.c_str()) == 0);
    assert(!first.update_password("blocked replacement"));
    assert(first.password() == "new passphrase");
    assert(unlink(temporary.c_str()) == 0);
    // A crash's leftover regular file must not block the change forever.
    {
        FILE *leftover = fopen(temporary.c_str(), "w");
        assert(leftover);
        fclose(leftover);
    }
    assert(first.update_password("after a crash"));
    assert(first.password() == "after a crash");
    assert(first.update_password(password));
    const auto alias = std::string(path) + "/alias";
    assert(symlink(directory.c_str(), alias.c_str()) == 0);
    term::ssh::Identity linked;
    assert(!linked.load_or_create(alias.c_str()));
    assert(unlink(alias.c_str()) == 0);
    assert(rename((directory + "/password").c_str(), (directory + "/saved-password").c_str()) == 0);
    assert(symlink("saved-password", (directory + "/password").c_str()) == 0);
    assert(!linked.load_or_create(directory.c_str()));
    assert(unlink((directory + "/password").c_str()) == 0);
    assert(rename((directory + "/saved-password").c_str(), (directory + "/password").c_str()) == 0);
    const auto hardlink = std::string(path) + "/linked-password";
    assert(link((directory + "/password").c_str(), hardlink.c_str()) == 0);
    assert(!linked.load_or_create(directory.c_str()));
    assert(unlink(hardlink.c_str()) == 0);
    // Optional key file must be bounded and private; unsupported options must
    // never be silently discarded (that would remove their restrictions).
    const auto authorized = directory + "/authorized_keys";
    auto write_authorized = [&](const std::string &text)
    {
        FILE *file = std::fopen(authorized.c_str(), "wb");
        assert(file);
        assert(std::fwrite(text.data(), 1, text.size(), file) == text.size());
        assert(std::fclose(file) == 0);
        assert(chmod(authorized.c_str(), 0600) == 0);
    };
    write_authorized("# no keys yet\n\n");
    assert(linked.load_or_create(directory.c_str()));
    assert(linked.authorized_keys().empty());
    for (const auto &text : std::vector<std::string>{
             "ssh-rsa AAAA comment\n", "restrict ecdsa-sha2-nistp256 AAAA\n",
             "ecdsa-sha2-nistp256 !!!!\n", std::string(8193, 'x'), std::string("# bad\0line", 10)})
    {
        write_authorized(text);
        assert(!linked.load_or_create(directory.c_str()));
        assert(linked.authorized_keys().empty() && linked.password().empty());
    }
    write_authorized("");
    assert(linked.load_or_create(directory.c_str()));
    assert(chmod(authorized.c_str(), 0644) == 0);
    assert(!linked.load_or_create(directory.c_str()));
    assert(unlink(authorized.c_str()) == 0);
    assert(symlink("password", authorized.c_str()) == 0);
    assert(!linked.load_or_create(directory.c_str()));
    assert(unlink(authorized.c_str()) == 0);
    assert(chmod((directory + "/password").c_str(), 0644) == 0);
    term::ssh::Identity unsafe;
    assert(!unsafe.load_or_create(directory.c_str()));
    assert(unsafe.password().empty() && unsafe.key().empty());
    assert(chmod((directory + "/password").c_str(), 0600) == 0);
    assert(truncate((directory + "/host-key.der").c_str(), 1) == 0);
    term::ssh::Identity damaged;
    assert(!damaged.load_or_create(directory.c_str()));
    assert(unlink((directory + "/host-key.der").c_str()) == 0);
    // Missing one half is corruption, not permission to rotate the other half.
    assert(!damaged.load_or_create(directory.c_str()));
    assert(unlink((directory + "/password").c_str()) == 0);
    assert(rmdir(directory.c_str()) == 0);
    assert(rmdir(path) == 0);
}
