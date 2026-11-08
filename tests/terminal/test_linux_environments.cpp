// PS5Term GPL-3.0-or-later. Reading `ps5term-env list --json`.
#include "linux_environments.hpp"
#include <cassert>
#include <cstdio>
#include <string_view>

using term::LinuxEnvironment;
int main()
{
    term::LinuxEnvironments list;
    // Exactly what the guest prints (tooling/subsystem/guest/overlay/usr/bin/ps5term-env).
    constexpr std::string_view report =
        R"({"default":"ubuntu","data":true,"disk_kib":491316,"used_kib":350212,"environments":[)"
        R"({"name":"alpine","title":"Alpine 3.23","manager":"apk","state":"absent","kib":0,"need_kib":65536},)"
        R"({"name":"ubuntu","title":"Ubuntu 24.04 LTS","manager":"apt","state":"installed","kib":338534,"need_kib":393216}]})"
        "\n";
    assert(term::parse_linux_environments(report, list));
    assert(list.known && list.data);
    assert(list.default_environment == LinuxEnvironment::ubuntu);
    assert(list.disk_kib == 491316 && list.used_kib == 350212);
    assert(list.installed[0] && !list.installed[1] && list.installed[2]);
    assert(list.kib[2] == 338534 && list.kib[1] == 0);

    // A newer guest's extra environment is skipped, not an error.
    term::LinuxEnvironments newer;
    assert(term::parse_linux_environments(
        R"({"default":"base","data":false,"disk_kib":0,"used_kib":0,"environments":[)"
        R"({"name":"debian","title":"Debian 13","manager":"apt","state":"installed","kib":9,"need_kib":1}]})",
        newer));
    assert(newer.known && !newer.data && newer.default_environment == LinuxEnvironment::base);
    assert(newer.installed[0] && !newer.installed[1] && !newer.installed[2]);

    // Anything else leaves the previous state alone.
    const auto before = list;
    for (
        std::string_view bad :
        {std::string_view{},
         std::string_view{"ps5term-env: requires the PS5Term RISC-V Linux guest"},
         std::string_view{
             R"({"default":"gentoo","data":true,"disk_kib":1,"used_kib":1,"environments":[]})"},
         std::string_view{R"({"default":"base","data":true,"environments":[]})"},
         std::string_view{
             R"({"default":"base","data":true,"disk_kib":1,"used_kib":1,"environments":[{"name":"alpine")"},
         std::string_view{
             R"({"default":"base","data":true,"disk_kib":1,"used_kib":1,"environments":[{"name":"alpine","state":"absent"}]})"}})
    {
        assert(!term::parse_linux_environments(bad, list));
        assert(list.default_environment == before.default_environment &&
               list.kib[2] == before.kib[2] && list.disk_kib == before.disk_kib);
    }
    // The fixed table matches the guest's names.
    assert(std::string_view(term::linux_environment_info(LinuxEnvironment::alpine).name) ==
           "alpine");
    assert(std::string_view(term::linux_environment_info(LinuxEnvironment::ubuntu).name) ==
           "ubuntu");
    assert(term::linux_environment_info(LinuxEnvironment::ubuntu).need_mib == 384);
    std::puts("PASS: Linux environment list parsing, unknown names and malformed reports");
}
