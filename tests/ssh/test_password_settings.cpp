// GPL-3.0-or-later. Real owned identity plus controller/USB Settings routing.
#include "src/app.hpp"
#include "backend/owned_ssh/identity.hpp"
#include <cassert>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>
namespace term
{
struct AppTest
{
    static void run(const char *directory)
    {
        App app;
        std::destroy_at(&app.ssh_);
        std::construct_at(&app.ssh_, directory, 0, 0, 0);
        app.editor_.insert("keep my command");
        app.controller(0x0008, 0);
        app.controller(0x8000, 0);
        assert(app.password_entry_.active() && !app.ssh_enabled_);
        for (unsigned i = 0; i < 8; ++i)
            app.controller(0x4000, 0);
        app.controller(0x0200, 0);
        assert(app.password_entry_.confirming());
        for (unsigned i = 0; i < 8; ++i)
            app.controller(0x4000, 0);
        app.controller(0x0200, 0);
        assert(!app.password_entry_.active() && !app.ssh_.listening());
        ssh::Identity identity;
        assert(identity.load_or_create(directory) && identity.password() == "11111111");
        app.key(Key::edit_password);
        for (char c : std::string("custom pass"))
            app.key(Key::character, c);
        app.key(Key::copy);
        app.key(Key::paste);
        app.key(Key::reveal_password);
        assert(!app.password_visible_ && app.clipboard_.text().empty());
        app.key(Key::enter);
        app.key(Key::character, 'x');
        app.key(Key::enter);
        assert(app.password_entry_.active() && app.password_entry_.size() == 0);
        for (char c : std::string("custom pass"))
            app.key(Key::character, c);
        app.key(Key::enter);
        assert(!app.password_entry_.active());
        assert(identity.load_or_create(directory) && identity.password() == "custom pass");
        assert(app.editor_.text() == "keep my command");
        app.key(Key::edit_password);
        app.key(Key::character, 's');
        app.key(Key::escape);
        assert(!app.password_entry_.active() && app.password_entry_.value().empty() &&
               app.settings_open_);
        app.key(Key::edit_password);
        app.controller(0x0008, 0);
        assert(!app.settings_open_ && !app.password_entry_.active());
    }
};
} // namespace term
int main()
{
    char path[] = "/tmp/ps5term-password-settings-XXXXXX";
    assert(mkdtemp(path));
    const auto directory = std::string(path) + "/ssh";
    term::AppTest::run(directory.c_str());
    unlink((directory + "/password").c_str());
    unlink((directory + "/host-key.der").c_str());
    assert(rmdir(directory.c_str()) == 0 && rmdir(path) == 0);
}
