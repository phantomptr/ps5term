/*
 * ps5-native-app-boilerplate - Host tests for reusable application behavior.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Validates bounded asset loading without requiring PS5 hardware.
 */

#include "demo_renderer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <span>
#include <vector>

namespace
{
class AssetTextTest : public testing::Test
{
  protected:
    static constexpr const char *missing_path = "build/tests/missing-banner.txt";
    static constexpr const char *asset_path = "build/tests/banner.txt";

    void SetUp() override
    {
        (void)std::remove(missing_path);
        std::ofstream asset{asset_path, std::ios::binary};
        ASSERT_TRUE(asset.is_open());
        asset << "FROM APP0\r\n";
        ASSERT_TRUE(asset.good());
    }

    void TearDown() override
    {
        (void)std::remove(asset_path);
        (void)std::remove(missing_path);
    }
};

TEST_F(AssetTextTest, MissingAssetUsesFallback)
{
    std::array<char, 32> text{};
    ps5::demo::read_asset_text(missing_path, std::span{text}, "FALLBACK");
    EXPECT_STREQ(text.data(), "FALLBACK");
}

TEST_F(AssetTextTest, AssetTextTrimsLineEndings)
{
    std::array<char, 32> text{};
    ps5::demo::read_asset_text(asset_path, std::span{text}, "FALLBACK");
    EXPECT_STREQ(text.data(), "FROM APP0");
}

TEST_F(AssetTextTest, AssetTextRemainsNullTerminatedWhenTruncated)
{
    std::array<char, 5> bounded{};
    ps5::demo::read_asset_text(asset_path, std::span{bounded}, "FALLBACK");
    EXPECT_STREQ(bounded.data(), "FROM");
}

TEST(TerminalFont, AntialiasedGlyphAndClippedWrites)
{
    constexpr std::size_t words = 0x1000000 / sizeof(std::uint32_t);
    constexpr std::uint32_t background = 0xff000000, guard = 0x12345678;
    std::vector<std::uint32_t> buffer(words + 64, background);
    std::fill(buffer.begin() + words, buffer.end(), guard);
    ps5::demo::Canvas canvas(buffer.data());
    canvas.text(96, 226, "Wg", 2, ps5::demo::Color::white);
    bool partial = false, ink = false;
    for (std::size_t i = 0; i < words; ++i)
    {
        ink |= buffer[i] != background;
        partial |= buffer[i] != background && buffer[i] != 0xffffffff;
    }
    EXPECT_TRUE(ink);
    EXPECT_TRUE(partial);
    canvas.text(1919, 1079, "clipped", 3, ps5::demo::Color::white);
    canvas.text(0xffffffff, 0xffffffff, "outside", 2, ps5::demo::Color::white);
    for (std::size_t i = words; i < buffer.size(); ++i)
        EXPECT_EQ(buffer[i], guard);
}
TEST(TerminalFont, UnicodeGlyphsFillTheirCells)
{
    constexpr std::size_t words = 0x1000000 / sizeof(std::uint32_t);
    constexpr std::uint32_t background = 0xff000000;
    std::vector<std::uint32_t> buffer(words, background);
    ps5::demo::Canvas canvas(buffer.data());
    const auto ink = [&](unsigned x, unsigned y)
    {
        std::fill(buffer.begin(), buffer.end(), background);
        (void)x;
        (void)y;
    };
    const auto pixels = [&]()
    {
        std::size_t count = 0;
        for (const auto value : buffer)
            count += value != background;
        return count;
    };
    ink(0, 0);
    EXPECT_TRUE(canvas.glyph(96, 226, 0x2588, ps5::demo::Color::white)); // Full block
    EXPECT_EQ(pixels(), 10U * 20U); // Exactly one cell, every pixel.
    ink(0, 0);
    EXPECT_TRUE(canvas.glyph(96, 226, 0x2500, ps5::demo::Color::white)); // Box line
    EXPECT_GE(pixels(), 10U); // Reaches both edges so adjacent cells join.
    for (const std::uint32_t known :
         {0x00E9U, 0x0416U, 0x03A9U, 0x2192U, 0x2713U, 0xE0B0U, 0x2591U, 0x256DU})
    {
        ink(0, 0);
        EXPECT_TRUE(canvas.glyph(96, 226, known, ps5::demo::Color::white)) << known;
        EXPECT_GT(pixels(), 0U) << known;
    }
    ink(0, 0);
    EXPECT_FALSE(canvas.glyph(96, 226, 0x4E2D, ps5::demo::Color::white)); // No CJK in the atlas.
    EXPECT_GT(pixels(), 0U); // A visible replacement, never a blank that hides text.
    EXPECT_FALSE(canvas.glyph(0xffffffff, 0xffffffff, 0x2588, ps5::demo::Color::white));
}
} // namespace
