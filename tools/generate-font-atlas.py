#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bake the pinned OFL font into antialiased cells; run with Pillow 12.0.0.
ASCII exists at every UI size. The terminal size also gets a sorted table of
extra glyphs. Box drawing and block elements are drawn to fill the cell exactly,
because font outlines leave gaps between rows at this line height.
"""
import hashlib
import json
import math
from pathlib import Path
import unicodedata
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'third_party/jetbrains-mono'
OVERSAMPLE = 4
EXTRA_FROM_FONT = [
    (0x00A1, 0x00FF), (0x0100, 0x017F), (0x0192, 0x0192), (0x02C6, 0x02DD),
    (0x0391, 0x03C9), (0x0401, 0x045F), (0x2010, 0x2027), (0x2030, 0x203A),
    (0x20AC, 0x20AC), (0x2122, 0x2122), (0x2190, 0x2199), (0x21B5, 0x21B5),
    (0x21CC, 0x21CC), (0x2200, 0x2265), (0x2318, 0x2318), (0x23CE, 0x23CE),
    (0x25A0, 0x25A1), (0x25AA, 0x25AB), (0x25B2, 0x25C7), (0x25CB, 0x25CF),
    (0x25E2, 0x25E5), (0x2605, 0x2606), (0x2713, 0x2717), (0x276F, 0x276F),
    (0xE0A0, 0xE0A3), (0xE0B0, 0xE0B3), (0xFFFD, 0xFFFD),
]


def cell_image(width, height):
    return Image.new('L', (width * OVERSAMPLE, height * OVERSAMPLE))


def draw_box(code, width, height):
    """One box-drawing character, from its Unicode name, edge to edge."""
    image = cell_image(width, height)
    draw = ImageDraw.Draw(image)
    w, h = image.size
    cx, cy = w // 2, h // 2
    light = max(OVERSAMPLE, round(w * 0.12))
    heavy = light * 2
    def bar(x0, y0, x1, y1):
        draw.rectangle([x0, y0, x1 - 1, y1 - 1], fill=255)
    def vertical(y0, y1, thick, offset=0):
        bar(cx + offset - thick // 2, y0, cx + offset - thick // 2 + thick, y1)
    def horizontal(x0, x1, thick, offset=0):
        bar(x0, cy + offset - thick // 2, x1, cy + offset - thick // 2 + thick)
    name = unicodedata.name(chr(code)).removeprefix('BOX DRAWINGS ')
    if code in (0x2571, 0x2572, 0x2573):
        if code != 0x2572: draw.line([(0, h), (w, 0)], fill=255, width=light)
        if code != 0x2571: draw.line([(0, 0), (w, h)], fill=255, width=light)
        return image
    if 0x256D <= code <= 0x2570:  # Rounded corners.
        r = min(w, h) // 3
        ends = {0x256D: ('down', 'right'), 0x256E: ('down', 'left'), 0x256F: ('up', 'left'), 0x2570: ('up', 'right')}[code]
        sx, sy = (1 if ends[1] == 'right' else -1), (1 if ends[0] == 'down' else -1)
        x_end = w if sx > 0 else 0
        y_end = h if sy > 0 else 0
        points = []
        for i in range(33):
            t = i / 32 * math.pi / 2
            points.append((cx + sx * r * (1 - math.cos(t)), cy + sy * r * (1 - math.sin(t))))
        draw.line(points, fill=255, width=light, joint='curve')
        bar(min(cx + sx * r, x_end), cy - light // 2, max(cx + sx * r, x_end), cy - light // 2 + light)
        bar(cx - light // 2, min(cy + sy * r, y_end), cx - light // 2 + light, max(cy + sy * r, y_end))
        return image
    words = name.split()
    if 'DASH' in name:  # Dashed lines keep the look of their solid forms.
        thick = heavy if 'HEAVY' in name else light
        count = {'DOUBLE': 2, 'TRIPLE': 3, 'QUADRUPLE': 4}[words[1]]
        if 'VERTICAL' in name:
            for i in range(count):
                vertical(i * h // count, i * h // count + h // (count * 2), thick)
        else:
            for i in range(count):
                horizontal(i * w // count, i * w // count + w // (count * 2), thick)
        return image
    # Weight per direction: 1 light, 2 heavy, 3 double.
    arms = {}
    weight = {'LIGHT': 1, 'HEAVY': 2, 'DOUBLE': 3, 'SINGLE': 1}
    expand = {'VERTICAL': ('UP', 'DOWN'), 'HORIZONTAL': ('LEFT', 'RIGHT')}
    default = None
    current = []
    for word in words:
        if word == 'AND':
            current = []
        elif word in weight:
            for direction in current:
                arms[direction] = weight[word]
            if not current:
                default = weight[word]
            current = []
        elif word in expand or word in ('UP', 'DOWN', 'LEFT', 'RIGHT'):
            current = list(current) + list(expand.get(word, (word,)))
            for direction in expand.get(word, (word,)):
                arms.setdefault(direction, None)
    for direction, value in list(arms.items()):
        if value is None:
            arms[direction] = default or 1
    gap = heavy
    for direction, value in arms.items():
        thick = heavy if value == 2 else light
        vertical_arm = direction in ('UP', 'DOWN')
        y0, y1 = (0, cy + thick // 2) if direction == 'UP' else (cy - thick // 2, h)
        x0, x1 = (0, cx + thick // 2) if direction == 'LEFT' else (cx - thick // 2, w)
        if value == 3:
            for offset in (-gap // 2, gap // 2):
                if vertical_arm: vertical(y0 if direction == 'DOWN' else 0, y1 if direction == 'UP' else h, light, offset)
                else: horizontal(x0 if direction == 'RIGHT' else 0, x1 if direction == 'LEFT' else w, light, offset)
        elif vertical_arm:
            vertical(y0, y1, thick)
        else:
            horizontal(x0, x1, thick)
    return image


def draw_block(code, width, height):
    """Block elements U+2580..U+259F: exact fractions of the cell."""
    image = cell_image(width, height)
    draw = ImageDraw.Draw(image)
    w, h = image.size
    def fill(x0, y0, x1, y1, value=255):
        draw.rectangle([round(x0 * w), round(y0 * h), round(x1 * w) - 1, round(y1 * h) - 1], fill=value)
    if code == 0x2580: fill(0, 0, 1, .5)
    elif 0x2581 <= code <= 0x2588: fill(0, 1 - (code - 0x2580) / 8, 1, 1)
    elif 0x2589 <= code <= 0x258F: fill(0, 0, (0x2590 - code) / 8, 1)
    elif code == 0x2590: fill(.5, 0, 1, 1)
    elif 0x2591 <= code <= 0x2593: fill(0, 0, 1, 1, (code - 0x2590) * 64)
    elif code == 0x2594: fill(0, 0, 1, .125)
    elif code == 0x2595: fill(.875, 0, 1, 1)
    else:
        quadrants = {0x2596: 'c', 0x2597: 'd', 0x2598: 'a', 0x2599: 'acd', 0x259A: 'ad', 0x259B: 'abc',
                     0x259C: 'abd', 0x259D: 'b', 0x259E: 'bc', 0x259F: 'bcd'}[code]
        for part in quadrants:
            x, y = {'a': (0, 0), 'b': (.5, 0), 'c': (0, .5), 'd': (.5, .5)}[part]
            fill(x, y, x + .5, y + .5)
    return image


def main():
    records = json.loads((FONT / 'SOURCE.json').read_text())['files']
    for record in records:
        if hashlib.sha256((FONT / record['file']).read_bytes()).hexdigest() != record['sha256']:
            raise RuntimeError('Pinned font source hash mismatch')
    path = FONT / 'JetBrainsMono-Regular.ttf'
    available = set(TTFont(str(path)).getBestCmap())
    output = ['// Generated by tools/generate-font-atlas.py. Code GPL-3.0-or-later; glyph data from',
              '// JetBrains Mono under SIL OFL 1.1 (third_party/jetbrains-mono).',
              '#pragma once', '#include <cstdint>', 'namespace ps5::demo::font {']
    def emit(name, pixels):
        output.append(f'inline constexpr std::uint8_t {name}[] = {{')
        for start in range(0, len(pixels), 32):
            output.append(','.join(map(str, pixels[start:start + 32])) + ',')
        output.append('};')
    for style, size, width, height in [(1, 12, 7, 16), (2, 16, 10, 20), (3, 24, 15, 30)]:
        font = ImageFont.truetype(str(path), size * 2)
        def from_font(code):
            cell = Image.new('L', (width * 2, height * 2))
            # Common baseline preserves capitals, punctuation and descenders.
            ImageDraw.Draw(cell).text((0, size * 2), chr(code), font=font, fill=255, anchor='ls')
            return cell.resize((width, height), Image.Resampling.LANCZOS).tobytes()
        pixels = bytearray()
        for code in range(32, 127):
            pixels.extend(from_font(code))
        emit(f'atlas{style}', pixels)
        if style != 2:
            continue
        drawn = {code: draw_box for code in range(0x2500, 0x2580)}
        drawn.update({code: draw_block for code in range(0x2580, 0x25A0)})
        codes = {code for first, last in EXTRA_FROM_FONT for code in range(first, last + 1)
                 if code in available and unicodedata.category(chr(code)) not in ('Cn', 'Mn', 'Cc', 'Cf')}
        codes.update(drawn)
        table = sorted(codes)
        extra = bytearray()
        for code in table:
            if code in drawn:
                image = drawn[code](code, width, height).resize((width, height), Image.Resampling.BOX)
                extra.extend(image.tobytes())
            else:
                extra.extend(from_font(code))
        output.append('inline constexpr std::uint32_t extra_codes[] = {')
        for start in range(0, len(table), 12):
            output.append(','.join(hex(c) for c in table[start:start + 12]) + ',')
        output.append('};')
        emit('extra2', extra)
    output.append('} // namespace ps5::demo::font\n')
    (ROOT / 'src/font_atlas.hpp').write_text('\n'.join(output))
    print('terminal glyphs beyond ASCII:', len(table))


if __name__ == '__main__':
    main()
