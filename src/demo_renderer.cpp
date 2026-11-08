/*
 * ps5-native-app-boilerplate - CPU VideoOut demonstration implementation.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides the bounded drawing surface and PS5 presentation loop used by the
 * editable starter application.
 */

#include "demo_renderer.hpp"
#include "font_atlas.hpp"
#include "text_metrics.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceSystemServiceHideSplashScreen();
    int open(const char *path, int flags, ...);
    long read(int descriptor, void *buffer, std::size_t size);
    int close(int descriptor);
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
    bool ps5ObserveOwnedAllocation(const void *address) noexcept;
}

namespace ps5::demo
{
namespace
{
constexpr unsigned frame_width = 1920;
constexpr unsigned frame_height = 1080;
constexpr std::size_t frame_bytes = 0x1000000;
constexpr std::size_t memory_bytes = frame_bytes * 2;
constexpr std::size_t memory_alignment = 0x200000;
constexpr int memory_type_wc_garlic = 3;
constexpr int map_protection = 0x33;
constexpr std::uint64_t pixel_format_rgba8_srgb = UINT64_C(0x8000000022000000);

struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};

struct VideoAttribute
{
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

class File final
{
  public:
    explicit File(int descriptor = -1) noexcept : descriptor_{descriptor}
    {
    }

    ~File()
    {
        reset();
    }

    File(const File &) = delete;
    File &operator=(const File &) = delete;

    File(File &&other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)}
    {
    }

    File &operator=(File &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return descriptor_ >= 0;
    }

    [[nodiscard]] int get() const noexcept
    {
        return descriptor_;
    }

  private:
    void reset() noexcept
    {
        if (descriptor_ >= 0)
        {
            (void)close(descriptor_);
            descriptor_ = -1;
        }
    }

    int descriptor_;
};

class LifetimeProbe final
{
  public:
    explicit LifetimeProbe(bool &destroyed) noexcept : destroyed_{&destroyed}
    {
    }

    ~LifetimeProbe()
    {
        *destroyed_ = true;
    }

    LifetimeProbe(const LifetimeProbe &) = delete;
    LifetimeProbe &operator=(const LifetimeProbe &) = delete;

  private:
    bool *destroyed_;
};

NotificationRequest notification{};

void copy_message(std::span<char> destination, std::string_view source) noexcept
{
    if (destination.empty())
        return;

    const std::size_t count =
        source.size() < destination.size() - 1 ? source.size() : destination.size() - 1;
    for (std::size_t index = 0; index < count; ++index)
        destination[index] = source[index];
    destination[count] = '\0';
}

void notify(std::string_view message) noexcept
{
    copy_message(std::span{notification.message}, message);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
}

[[nodiscard]] bool verify_unique_ownership() noexcept
{
    bool destroyed = false;
    {
        std::unique_ptr<LifetimeProbe> probe{new (std::nothrow_t{}) LifetimeProbe{destroyed}};
        if (!ps5ObserveOwnedAllocation(probe.get()))
            return false;
    }
    return destroyed;
}

[[noreturn]] void halt(std::string_view message) noexcept
{
    notify(message);
    for (;;)
        (void)sceKernelUsleep(1000000);
}

[[nodiscard]] constexpr std::size_t tiled_byte_offset(unsigned x, unsigned y) noexcept
{
    const std::uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                                 ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                                 ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    const std::uint32_t blocks_per_row = (frame_width + 127U) >> 7;
    const std::uint32_t block_index = (y >> 7) * blocks_per_row + (x >> 7);

    return (static_cast<std::size_t>(block_index) << 16) + offset;
}

void put_pixel_unchecked(std::uint32_t *pixels, unsigned x, unsigned y, Color color) noexcept
{
    auto *bytes = reinterpret_cast<std::uint8_t *>(pixels);
    *reinterpret_cast<std::uint32_t *>(bytes + tiled_byte_offset(x, y)) =
        static_cast<std::uint32_t>(color);
}

void fill_rect(std::uint32_t *pixels, unsigned x, unsigned y, unsigned width, unsigned height,
               Color color) noexcept
{
    if (x >= frame_width || y >= frame_height)
        return;

    const unsigned right = width > frame_width - x ? frame_width : x + width;
    const unsigned bottom = height > frame_height - y ? frame_height : y + height;
    for (unsigned row = y; row < bottom; ++row)
    {
        for (unsigned column = x; column < right; ++column)
            put_pixel_unchecked(pixels, column, row, color);
    }
}

void fill_circle(std::uint32_t *pixels, unsigned center_x, unsigned center_y, unsigned radius,
                 Color color) noexcept
{
    const int signed_radius = static_cast<int>(radius);
    for (int y = -signed_radius; y <= signed_radius; ++y)
    {
        for (int x = -signed_radius; x <= signed_radius; ++x)
        {
            if (x * x + y * y <= signed_radius * signed_radius)
            {
                const int pixel_x = static_cast<int>(center_x) + x;
                const int pixel_y = static_cast<int>(center_y) + y;
                if (pixel_x >= 0 && pixel_y >= 0)
                {
                    const auto bounded_x = static_cast<unsigned>(pixel_x);
                    const auto bounded_y = static_cast<unsigned>(pixel_y);
                    if (bounded_x < frame_width && bounded_y < frame_height)
                        put_pixel_unchecked(pixels, bounded_x, bounded_y, color);
                }
            }
        }
    }
}

void fill_triangle(std::uint32_t *pixels, unsigned center_x, unsigned top, unsigned half_width,
                   unsigned height, Color color) noexcept
{
    if (height == 0 || center_x >= frame_width)
        return;

    for (unsigned row = 0; row < height; ++row)
    {
        const unsigned half = row * half_width / height;
        const unsigned left = half > center_x ? 0 : center_x - half;
        const unsigned right = half >= frame_width - center_x ? frame_width : center_x + half + 1;
        fill_rect(pixels, left, top + row, right - left, 1, color);
    }
}

void draw_coverage(std::uint32_t *pixels, unsigned x, unsigned y, const std::uint8_t *coverage,
                   unsigned width, unsigned height, std::uint32_t foreground) noexcept;
void draw_text(std::uint32_t *pixels, unsigned x, unsigned y, std::string_view value,
               unsigned scale, Color color) noexcept
{
    const unsigned width = term::text_metrics::advance(scale);
    const unsigned height = term::text_metrics::height(scale);
    if (!width || x >= frame_width || y >= frame_height)
        return;
    const auto *atlas = scale == 1 ? font::atlas1 : scale == 2 ? font::atlas2 : font::atlas3;
    const auto foreground = static_cast<std::uint32_t>(color);
    for (const char character : value)
    {
        const auto code = static_cast<unsigned char>(character);
        const unsigned glyph = (code >= 32 && code <= 126 ? code : '?') - 32;
        draw_coverage(pixels, x, y, atlas + glyph * width * height, width, height, foreground);
        x += width;
        if (x >= frame_width)
            return;
    }
}
void draw_coverage(std::uint32_t *pixels, unsigned x, unsigned y, const std::uint8_t *coverage,
                   unsigned width, unsigned height, std::uint32_t foreground) noexcept
{
    {
        for (unsigned row = 0; row < height && row < frame_height - y; ++row)
        {
            for (unsigned column = 0; column < width && column < frame_width - x; ++column)
            {
                const unsigned alpha = coverage[row * width + column];
                if (!alpha)
                    continue;
                auto *bytes = reinterpret_cast<std::uint8_t *>(pixels);
                auto *pixel = reinterpret_cast<std::uint32_t *>(
                    bytes + tiled_byte_offset(x + column, y + row));
                const auto background = *pixel;
                std::uint32_t blended = 0xff000000;
                for (unsigned shift = 0; shift < 24; shift += 8)
                {
                    const unsigned from = (background >> shift) & 255;
                    const unsigned to = (foreground >> shift) & 255;
                    blended |= ((from * (255 - alpha) + to * alpha + 127) / 255) << shift;
                }
                *pixel = blended;
            }
        }
    }
}

void flush_range(void *address, std::size_t length) noexcept
{
#if defined(__x86_64__)
    auto *at = static_cast<std::uint8_t *>(address);
    const auto *end = at + length;

    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
#else
    (void)address;
    (void)length;
#endif
}
} // namespace

void Canvas::clear(Color color) noexcept
{
    fill_rect(pixels_, 0, 0, frame_width, frame_height, color);
}

void Canvas::rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                       Color color) noexcept
{
    fill_rect(pixels_, x, y, width, height, color);
}

void Canvas::circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept
{
    fill_circle(pixels_, center_x, center_y, radius, color);
}

void Canvas::triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                      Color color) noexcept
{
    fill_triangle(pixels_, center_x, top, half_width, height, color);
}

void Canvas::text(unsigned x, unsigned y, std::string_view value, unsigned scale,
                  Color color) noexcept
{
    // ASCII runs use the native atlases; other UTF-8 characters (translations: accented Latin,
    // Cyrillic) come from the Unicode atlas, one cell each.
    const unsigned width = term::text_metrics::advance(scale);
    std::size_t i = 0;
    while (i < value.size() && x < frame_width)
    {
        std::size_t run = i;
        while (run < value.size() && static_cast<unsigned char>(value[run]) < 0x80)
            ++run;
        if (run > i)
        {
            draw_text(pixels_, x, y, value.substr(i, run - i), scale, color);
            x += static_cast<unsigned>(run - i) * width;
            i = run;
            continue;
        }
        const auto lead = static_cast<unsigned char>(value[i]);
        std::size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        std::uint32_t code = lead & (length == 4 ? 0x07U : length == 3 ? 0x0FU : 0x1FU);
        for (std::size_t k = 1; k < length; ++k)
        {
            const auto next = i + k < value.size() ? static_cast<unsigned char>(value[i + k]) : 0;
            if ((next & 0xC0U) != 0x80U)
            {
                length = k; // Malformed: draw the replacement mark for what we have.
                code = 0xFFFD;
                break;
            }
            code = (code << 6) | (next & 0x3FU);
        }
        if (length == 1)
            code = 0xFFFD;
        glyph(x, y, code, color, scale);
        x += width;
        i += length;
    }
}

bool Canvas::glyph(unsigned x, unsigned y, std::uint32_t code_point, Color color,
                   unsigned style) noexcept
{
    if (x >= frame_width || y >= frame_height)
        return false;
    const unsigned width = term::text_metrics::advance(style),
                   height = term::text_metrics::height(style);
    if (!width || !height)
        return false;
    const auto foreground = static_cast<std::uint32_t>(color);
    if (code_point >= 32 && code_point <= 126)
    {
        draw_coverage(pixels_, x, y,
                      (style == 1   ? font::atlas1
                       : style == 3 ? font::atlas3
                                    : font::atlas2) +
                          (code_point - 32) * width * height,
                      width, height, foreground);
        return true;
    }
    const auto *first = std::begin(font::extra_codes), *last = std::end(font::extra_codes);
    const auto find = [&](std::uint32_t code)
    {
        const auto *at = std::lower_bound(first, last, code);
        return at != last && *at == code ? at : nullptr;
    };
    const auto *at = find(code_point);
    const bool known = at != nullptr;
    if (!at)
        at = find(0xFFFD);
    if (at)
    {
        const auto *source = font::extra2 + static_cast<std::size_t>(at - first) * 10 * 20;
        if (style == 2)
            draw_coverage(pixels_, x, y, source, width, height, foreground);
        else
        {
            // Resample the 10x20 Unicode atlas into the chosen cell (ASCII has native atlases):
            // an area average, so small text keeps its accents and large text stays smooth.
            std::uint8_t coverage[15 * 30];
            for (unsigned row = 0; row < height; ++row)
                for (unsigned col = 0; col < width; ++col)
                {
                    // Source rectangle in 1/(width*height) units of the 10x20 cell.
                    const unsigned x0 = col * 10, x1 = (col + 1) * 10, y0 = row * 20,
                                   y1 = (row + 1) * 20;
                    unsigned sum = 0, area = 0;
                    for (unsigned sy = y0 / height; sy * height < y1 && sy < 20; ++sy)
                        for (unsigned sx = x0 / width; sx * width < x1 && sx < 10; ++sx)
                        {
                            const unsigned ox = std::min(x1, (sx + 1) * width) -
                                                std::max(x0, sx * width),
                                           oy = std::min(y1, (sy + 1) * height) -
                                                std::max(y0, sy * height);
                            sum += source[sy * 10 + sx] * ox * oy;
                            area += ox * oy;
                        }
                    coverage[row * width + col] =
                        static_cast<std::uint8_t>(area ? (sum + area / 2) / area : 0);
                }
            draw_coverage(pixels_, x, y, coverage, width, height, foreground);
        }
    }
    return known;
}

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept
{
    copy_message(destination, fallback);
    File descriptor{open(path, 0)};
    if (!descriptor.valid() || destination.empty())
        return;

    const long count = read(descriptor.get(), destination.data(), destination.size() - 1);
    if (count <= 0)
        return;

    std::size_t length = static_cast<std::size_t>(count);
    while (length > 0 && (destination[length - 1] == '\r' || destination[length - 1] == '\n'))
        --length;
    destination[length] = '\0';
}

[[noreturn]] void run(DrawScene draw, std::string_view ready_message) noexcept
{
    if (!verify_unique_ownership())
        halt("PS5Term: unique ownership failed");
    if (draw == nullptr)
        halt("PS5Term: scene callback missing");

    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0)
        halt("PS5Term: sceVideoOutOpen failed");

    const std::size_t pool_size = sceKernelGetDirectMemorySize();
    if (pool_size < memory_bytes)
        halt("PS5Term: insufficient direct memory");

    std::int64_t physical_address = 0;
    int result =
        sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool_size), memory_bytes,
                                      memory_alignment, memory_type_wc_garlic, &physical_address);
    if (result < 0)
        halt("PS5Term: direct-memory allocation failed");

    void *mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, memory_bytes, map_protection, 0, physical_address,
                                      memory_alignment);
    if (result < 0)
        halt("PS5Term: direct-memory mapping failed");

    Canvas first{static_cast<std::uint32_t *>(mapped)};
    auto *second_frame = static_cast<std::uint8_t *>(mapped) + frame_bytes;
    Canvas second{reinterpret_cast<std::uint32_t *>(second_frame)};
    draw(first);
    draw(second);
    flush_range(mapped, memory_bytes);

    std::array<VideoBuffer, 2> buffers{{
        {mapped, nullptr, nullptr, nullptr},
        {second_frame, nullptr, nullptr, nullptr},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, frame_width,
                                   frame_height, 0, 0, 0);

    result = sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(),
                                         static_cast<std::int32_t>(buffers.size()), &attribute, 0,
                                         nullptr);
    if (result < 0)
        halt("PS5Term: buffer registration failed");
    if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0)
        halt("PS5Term: initial flip failed");

    (void)sceVideoOutWaitVblank(video);
    notify(ready_message);

    // Returning from main or calling exit crashes this launch context.
    unsigned frame = 1;
    for (;;)
    {
        Canvas &next = (frame & 1) ? second : first;
        draw(next);
        flush_range((frame & 1) ? static_cast<void *>(second_frame) : mapped, frame_bytes);
        (void)sceVideoOutSubmitFlip(video, static_cast<int>(frame & 1), 1, frame);
        (void)sceVideoOutWaitVblank(video);
        ++frame;
    }
}
} // namespace ps5::demo
