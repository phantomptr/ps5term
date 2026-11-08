// PS5Term - GPL-3.0-or-later.
#include "platform/directory.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
using term::platform::decode_directory_record;
using term::platform::DirectoryRecord;
int main()
{
    // FreeBSD 11 / SDK ABI: inode32, reclen16, type8, namlen8, name.
    std::array<char, 32> bytes{};
    bytes[0] = 1;
    bytes[4] = 16;
    bytes[7] = 3;
    std::memcpy(bytes.data() + 8, "abc", 4);
    auto record = decode_directory_record({bytes.data(), 16});
    assert(record.valid && record.bytes == 16 && record.name == "abc");
    assert(!decode_directory_record({bytes.data(), 15}).valid);
    assert(!decode_directory_record({bytes.data(), 7}).valid);
    bytes[4] = 0;
    assert(!decode_directory_record(bytes).valid);
    bytes[4] = 12;
    bytes[7] = 4;
    assert(!decode_directory_record(bytes).valid);
    bytes[7] = 3;
    bytes[11] = 'x';
    assert(!decode_directory_record(bytes).valid);
    bytes[11] = 0;
    bytes[9] = 0;
    assert(!decode_directory_record(bytes).valid);
    bytes[9] = 'b';
    bytes[4] = 13;
    assert(!decode_directory_record(bytes).valid);
    bytes[4] = 12;
    record = decode_directory_record(bytes);
    assert(record.valid && record.name == "abc" && record.bytes == 12);
    std::puts("directory record bounds tests passed");
}
