// PS5Term - bounded diagnostics persistence checks. GPL-3.0-or-later.
#include "diagnostics.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
int main()
{
    char directory[] = "/tmp/ps5term-diagnostics-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/status.json";
    term::DiagnosticSnapshot state;
    state.build_id = "test-build";
    state.instance_id = "instance-1";
    state.pid = 123;
    state.monotonic_us = 500;
    char output[2048]{};
    auto count = term::serialize_status(state, output, sizeof(output));
    assert(count > 0 && count < sizeof(output));
    assert(std::strstr(output, "\"schema\":1"));
    assert(std::strstr(output, "\"build_id\":\"test-build\""));
    assert(std::strstr(output, "\"terminal_backend\":\"line\""));
    assert(!std::strstr(output, "password"));
    assert(!term::serialize_status(state, output, 4));
    state.instance_id = "bad\"json";
    assert(!term::serialize_status(state, output, sizeof(output)));
    state.instance_id = "instance-1";
    assert(term::write_status(state, path.c_str()));
    struct stat st
    {
    };
    assert(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    std::ifstream file(path);
    std::string json((std::istreambuf_iterator<char>(file)), {});
    assert(json.find("\"pid\":123") != std::string::npos);
    state.monotonic_us = 600;
    assert(term::write_status(state, path.c_str()));
    assert(!term::write_status(state, "/does-not-exist/ps5term/status.json"));
    unlink(path.c_str());
    rmdir(directory);
}
