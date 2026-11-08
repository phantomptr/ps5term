// PS5Term - GPL-3.0-or-later.
#include "../../tooling/native/sce_module_writer.hpp"
#include <cassert>
#include <stdexcept>
using namespace ps5;
static elf::Section section(const char *name, std::uint64_t address, std::uint64_t offset,
                            std::uint64_t flags, std::size_t size, std::uint8_t fill = 0)
{
    elf::Section result;
    result.name = name;
    result.type = elf::kSectionProgBits;
    result.flags = flags;
    result.address = address;
    result.file_offset = offset;
    result.size = size;
    result.data.assign(size, fill);
    return result;
}
static std::uint64_t u64(const elf::Bytes &bytes, std::size_t at)
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= std::uint64_t(bytes.at(at + i)) << (8 * i);
    return value;
}
static elf::Image fixture(bool prefix)
{
    elf::Image image;
    image.sections.push_back(
        section(".text", 0, 0x4000, elf::kFlagAlloc | elf::kFlagExecute, 16, 0xcc));
    image.sections.push_back(section(".eh_frame_hdr", 0x4000, 0x8000, elf::kFlagAlloc, 8));
    if (prefix)
        image.sections.push_back(
            section(".data.rel.ro", 0x8000, 0xc000, elf::kFlagAlloc | elf::kFlagWrite, 16, 0xab));
    image.sections.push_back(section(".got", prefix ? 0x8010 : 0x8000, prefix ? 0xc010 : 0xc000,
                                     elf::kFlagAlloc | elf::kFlagWrite, 16, 0xcd));
    image.sections.push_back(
        section(".dynamic", 0x10000, 0x14000, elf::kFlagAlloc | elf::kFlagWrite, 16));
    image.sections.back().entry_size = 16;
    image.dynamic_symbols.resize(1);
    return image;
}
int main()
{
    for (bool prefix : {false, true})
    {
        auto image = fixture(prefix);
        auto output = module::write_executable(image, {});
        const std::size_t ph = 0x40 + 2 * 56;
        const auto offset = u64(output, ph + 8), address = u64(output, ph + 16),
                   align = u64(output, ph + 48);
        assert(offset == 0xc000 && address == 0x8000 && offset % align == address % align);
        assert(output.at(offset) == (prefix ? 0xab : 0xcd));
        assert(output.at(prefix ? 0xc010 : 0xc000) == 0xcd);
        // Process parameters must be mapped using this same segment origin.
        const std::size_t param = 0x40 + 5 * 56;
        assert(u64(output, param + 8) - offset == u64(output, param + 16) - address);
    }
    auto malformed = fixture(true);
    malformed.sections[2].file_offset += 8;
    bool rejected = false;
    try
    {
        (void)module::write_executable(malformed, {});
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    assert(rejected);
}
