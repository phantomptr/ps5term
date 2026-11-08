#!/usr/bin/env python3
"""PS5Term's app translations: English from the source, the rest from i18n/<code>.json.

SPDX-License-Identifier: GPL-3.0-or-later

    python3 tools/generate-i18n.py          check: every language has every string, no stale
                                            strings, the same printf conversions as the English,
                                            only characters the bundled font draws, and the
                                            generated table is current
    python3 tools/generate-i18n.py --write  also rewrite i18n/en.json (the strings to translate)
                                            and src/i18n_strings.inc (the tables tr() searches)

English is whatever the source wraps in tr("..."), trc("...") or N_("...") (adjacent literals
join, as in C++). A translation may be at most 1.5 times the English (plus 4) and 170 characters,
a full line of normal text; i18n/budgets.json sets the room for text with a fixed place on the
screen (buttons, menu items) or a full line of small text.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
I18N = ROOT / 'i18n'
OUTPUT = ROOT / 'src' / 'i18n_strings.inc'
# Settings cycles through these in this order, after Auto. Only scripts the font draws.
LANGUAGES = [
    ('en', 'English'),
    ('de', 'Deutsch'),
    ('es', 'Español'),
    ('fr', 'Français'),
    ('it', 'Italiano'),
    ('pl', 'Polski'),
    ('pt-BR', 'Português (Brasil)'),
    ('ru', 'Русский'),
    ('tr', 'Türkçe'),
    ('hu', 'Magyar'),
    ('id', 'Bahasa Indonesia'),
    ('vi', 'Tiếng Việt'),
    ('zh-CN', '简体中文'),
    ('zh-TW', '繁體中文'),
    ('ja', '日本語'),
    ('ko', '한국어'),
    ('ar', 'العربية'),
    ('fa', 'فارسی'),
    ('hi', 'हिन्दी'),
    ('bn', 'বাংলা'),
    ('th', 'ไทย'),
]
# Scripts the bundled font cannot draw are rendered ahead of time, sentence by sentence
# (src/i18n_runs.inc), with a shaping engine: joined Arabic, Indic clusters, wide CJK.
RENDERED = {'vi', 'zh-CN', 'zh-TW', 'ja', 'ko', 'ar', 'fa', 'hi', 'bn', 'th'}


def cells(text):
    """Width in terminal cells: East Asian wide characters take two, combining marks none."""
    import unicodedata
    width = 0
    for ch in text:
        if unicodedata.combining(ch) or unicodedata.category(ch) in ('Mn', 'Me', 'Cf'):
            continue
        width += 2 if unicodedata.east_asian_width(ch) in ('W', 'F') else 1
    return width
MARK = re.compile(r'\b(?:tr|trc|N_)\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)\)')
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
PRINTF = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcsp%]')


def unescape(body):
    return body.encode('utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')


def escape(text):
    out = []
    for ch in text:
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        else:
            out.append(ch)
    return ''.join(out)


def source_strings():
    found = {}
    for path in sorted((ROOT / 'src').rglob('*')):
        if path.suffix not in ('.cpp', '.hpp', '.h') or path.name == 'i18n_strings.inc':
            continue
        text = path.read_text(encoding='utf-8')
        for match in MARK.finditer(text):
            english = ''.join(unescape(part) for part in LITERAL.findall(match.group(1)))
            found.setdefault(english, f'{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}')
    return found


def font_codes():
    text = (ROOT / 'src' / 'font_atlas.hpp').read_text(encoding='utf-8')
    body = re.search(r'extra_codes\[\]\s*=\s*\{([^}]*)\}', text).group(1)
    return {int(value, 0) for value in re.findall(r'0x[0-9a-fA-F]+|\d+', body)}


# Rendered languages: font, pixel size at the normal text size (10x20 cells), shaping language.
FONTS = {
    'vi': ('third_party/jetbrains-mono/JetBrainsMono-Regular.ttf', 16.67, 'vi', 'ltr'),
    'zh-CN': ('.deps/fonts/NotoSansSC[wght].ttf', 17, 'zh-Hans', 'ltr'),
    'zh-TW': ('.deps/fonts/NotoSansTC[wght].ttf', 17, 'zh-Hant', 'ltr'),
    'ja': ('.deps/fonts/NotoSansJP[wght].ttf', 17, 'ja', 'ltr'),
    'ko': ('.deps/fonts/NotoSansKR[wght].ttf', 17, 'ko', 'ltr'),
    'ar': ('.deps/fonts/NotoSansArabic[wdth,wght].ttf', 18, 'ar', 'rtl'),
    'fa': ('.deps/fonts/NotoSansArabic[wdth,wght].ttf', 18, 'fa', 'rtl'),
    'hi': ('.deps/fonts/NotoSansDevanagari[wdth,wght].ttf', 16, 'hi', 'ltr'),
    'bn': ('.deps/fonts/NotoSansBengali[wdth,wght].ttf', 16, 'bn', 'ltr'),
    'th': ('.deps/fonts/NotoSansThai[wdth,wght].ttf', 17, 'th', 'ltr'),
}
RUN_HEIGHT, RUN_BASELINE, SUPERSAMPLE = 28, 20, 2  # 4 px above the 20 px cell, baseline 16 in it
MARKER = '\ue000'  # private-use: "draw rendered run NNNN" (4 hex digits) for the renderer
RTL = '\ue001'  # private-use, first in a right-to-left translation: draw its pieces in reverse


def units(text, direction):
    """Splits a translation into printf conversions / ASCII pieces (drawn by the app's font)
    and pieces to render. Right-to-left text shows its units in reverse order."""
    parts, pos = [], 0
    for match in PRINTF.finditer(text):
        if match.start() > pos:
            parts.append(text[pos:match.start()])
        parts.append(match.group(0))
        pos = match.end()
    if pos < len(text):
        parts.append(text[pos:])
    if direction == 'rtl':
        # Latin spans (paths, commands, names) leave the right-to-left text and are drawn by
        # the app's font, left to right, in their right-to-left place: order is then exact.
        split = []
        for part in parts:
            if PRINTF.fullmatch(part):
                split.append(part)
                continue
            pos = 0
            for match in re.finditer(r'[!-~\u00c0-\u024f](?:[ -~\u00c0-\u024f]*[!-~\u00c0-\u024f])?', part):
                start, end = match.start(), match.end()
                core = match.group(0)
                start += len(core) - len(core.lstrip(':;,.!?'))
                end -= len(core) - len(core.rstrip(':;,.!?'))
                # A bracket at an edge without its partner inside belongs to the outer text.
                pairs = {'(': ')', '[': ']', '{': '}', '<': '>'}
                changed = True
                while changed and start < end:
                    changed = False
                    span = part[start:end]
                    if span[0] in pairs and span.count(span[0]) > span.count(pairs[span[0]]):
                        start, changed = start + 1, True
                    elif span[-1] in pairs.values():
                        opener = next(k for k, v in pairs.items() if v == span[-1])
                        if span.count(span[-1]) > span.count(opener):
                            end, changed = end - 1, True
                    elif span[-1] in pairs and span.count(span[-1]) > span.count(pairs[span[-1]]):
                        end, changed = end - 1, True
                    elif span[0] in pairs.values():
                        opener = next(k for k, v in pairs.items() if v == span[0])
                        if span.count(span[0]) > span.count(opener):
                            start, changed = start + 1, True
                    # A separator (" / ", " - ") at an edge joins the outer text too.
                    span = part[start:end]
                    trimmed = re.sub(r'\s+[/|\-]\s*$', '', span)
                    trimmed = re.sub(r'^\s*[/|\-]\s+', '', trimmed)
                    if trimmed != span and trimmed:
                        offset = span.index(trimmed)
                        start, end = start + offset, start + offset + len(trimmed)
                        changed = True
                    while start < end and part[start] == ' ':
                        start += 1
                    while start < end and part[end - 1] == ' ':
                        end -= 1
                if start < end and re.search(r'[A-Za-z0-9]', part[start:end]):
                    if pos < start:
                        split.append(part[pos:start])
                    split.append(part[start:end])
                    pos = end
            if pos < len(part):
                split.append(part[pos:])
        parts = split
    out = []
    for part in parts:
        plain = PRINTF.fullmatch(part) is not None or all(ord(ch) < 128 or 0xc0 <= ord(ch) <= 0x24f for ch in part)
        if direction == 'rtl' and plain and not PRINTF.fullmatch(part) and not part.strip():
            plain = False  # spacing between right-to-left pieces stays with them
        if out and plain and out[-1][0] == 'plain' and direction != 'rtl':
            out[-1] = ('plain', out[-1][1] + part)
        else:
            out.append(('plain' if plain else 'render', part))
    if direction == 'rtl':
        # Spacing between two left-to-right pieces belongs to them: one piece, drawn as is.
        merged = []
        for i, (kind, part) in enumerate(out):
            between = (kind == 'render' and not part.strip() and 0 < i < len(out) - 1
                       and out[i - 1][0] == 'plain' and out[i + 1][0] == 'plain')
            if (kind == 'plain' or between) and merged and merged[-1][0] == 'plain':
                merged[-1] = ('plain', merged[-1][1] + part)
            else:
                merged.append(('plain' if between else kind, part))
        out = merged
    return out  # Logical order: printf arguments must match. The renderer draws RTL backwards.


class Renderer:
    def __init__(self, code):
        from PIL import ImageFont, features
        if not features.check('raqm'):
            sys.exit('Pillow without raqm cannot shape text; install Pillow with libraqm')
        path, size, self.language, self.direction = FONTS[code]
        if not (ROOT / path).exists():
            sys.exit(f'{path} is missing: run python3 tools/fetch-ui-fonts.py')
        self.font = ImageFont.truetype(str(ROOT / path), round(size * SUPERSAMPLE),
                                       layout_engine=ImageFont.Layout.RAQM)
        self.runs, self.index = [], {}

    def isolate(self, text):
        """Right-to-left text: each embedded Latin span (paths, commands, names) becomes its
        own left-to-right island, so /data/... and <public-key> keep their order."""
        if self.direction != 'rtl':
            return text
        span = re.compile(r'[!-~](?:[ -~]*[!-~])?')
        out, pos = [], 0
        for match in span.finditer(text):
            start, end = match.start(), match.end()
            core = match.group(0)
            lead = len(core) - len(core.lstrip(':;,.!?'))
            trail = len(core) - len(core.rstrip(':;,.!?'))
            start, end = start + lead, end - trail
            if start < end and re.search(r'[A-Za-z0-9]', text[start:end]):
                out.append(text[pos:start] + '\u2066' + text[start:end] + '\u2069')
                pos = end
        out.append(text[pos:])
        return ''.join(out)

    def run(self, text):
        """The run id for this text, rendering it once."""
        if text in self.index:
            return self.index[text]
        shaped = text
        from PIL import Image, ImageDraw
        options = dict(direction=self.direction, language=self.language)
        width = max(1, int(round(self.font.getlength(shaped, **options))))
        image = Image.new('L', (width, RUN_HEIGHT * SUPERSAMPLE))
        ImageDraw.Draw(image).text((0, RUN_BASELINE * SUPERSAMPLE), shaped, font=self.font, fill=255,
                                   anchor='ls', **options)
        small = image.resize(((width + SUPERSAMPLE - 1) // SUPERSAMPLE, RUN_HEIGHT), Image.Resampling.BOX)
        levels = bytes((value * 15 + 127) // 255 for value in small.tobytes())
        encoded, i = bytearray(), 0
        while i < len(levels):  # One byte: level << 4 | (repeat - 1), repeat up to 16 pixels.
            j = i
            while j < len(levels) and j - i < 16 and levels[j] == levels[i]:
                j += 1
            encoded.append(levels[i] << 4 | (j - i - 1))
            i = j
        self.index[text] = len(self.runs)
        self.runs.append((small.width, bytes(encoded)))
        return self.index[text]

    def text(self, value):
        """The table text for a translation, and its width in cells (conversions not counted)."""
        out, width = [RTL] if self.direction == 'rtl' else [], 0.0
        for kind, part in units(value, self.direction):
            if kind == 'plain':
                out.append(part)
                width += len(PRINTF.sub('', part))
            else:
                run = self.run(part)
                out.append(f'{MARKER}{run:04x}')
                width += self.runs[run][0] / 10
        return ''.join(out), width

    def file(self):
        """assets/i18n/<code>.runs: header, run table, then the run pixels."""
        import struct
        head = struct.pack('<4sHHHH', b'P5TR', 1, len(self.runs), RUN_HEIGHT, RUN_BASELINE)
        table, data = bytearray(), bytearray()
        for width, encoded in self.runs:
            table += struct.pack('<HHII', width, 0, len(data), len(encoded))
            data += encoded
        return head + bytes(table) + bytes(data)


def inputs_digest():
    """What the generated files depend on, so the check needs no fonts."""
    import hashlib
    digest = hashlib.sha256(Path(__file__).read_bytes())
    for path in sorted(I18N.glob('*.json')) + [ROOT / 'third_party' / 'noto' / 'SOURCE.json']:
        digest.update(path.name.encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


def generate(translations, rendered_text, runs_digests, rendered_names):
    lines = ['// Generated by tools/generate-i18n.py from i18n/*.json. Do not edit.',
             '// SPDX-License-Identifier: GPL-3.0-or-later',
             f'// inputs {inputs_digest()}']
    lines += [f'// runs {code} {digest}' for code, digest in sorted(runs_digests.items())]
    names = []
    for code, name in LANGUAGES[1:]:
        if code not in translations:
            continue
        symbol = 'strings_' + code.replace('-', '_')
        values = rendered_text.get(code, translations[code])
        pairs = sorted(values.items(), key=lambda pair: pair[0].encode('utf-8'))
        lines.append(f'constexpr Entry {symbol}[] = {{')
        lines += [f'    {{"{escape(english)}", "{escape(text)}"}},' for english, text in pairs]
        lines.append('};')
        names.append((code, rendered_names.get(code, name), symbol))
    lines.append('constexpr Table tables[] = {')
    lines.append('    {"en", "English", nullptr, 0, false},')
    lines += [f'    {{"{code}", "{escape(name)}", {symbol}, std::size({symbol}), {"true" if code in RENDERED else "false"}}},'
              for code, name, symbol in names]
    lines.append('};')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()

    english = source_strings()
    problems = []
    if args.write:
        (I18N / 'en.json').write_text(json.dumps({k: k for k in sorted(english)}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    saved = json.loads((I18N / 'en.json').read_text(encoding='utf-8')) if (I18N / 'en.json').exists() else {}
    if set(saved) != set(english):
        problems.append('i18n/en.json is out of date (run with --write)')

    budgets_path = I18N / 'budgets.json'
    budgets = json.loads(budgets_path.read_text(encoding='utf-8')) if budgets_path.exists() else {}
    for key in budgets:
        if key not in english:
            problems.append(f'budgets.json names a string the source no longer has: {key!r}')

    drawable = font_codes()
    translations = {}
    for code, _ in LANGUAGES[1:]:
        path = I18N / f'{code}.json'
        if not path.exists():
            problems.append(f'{code}: no {path.relative_to(ROOT)}')
            continue
        table = json.loads(path.read_text(encoding='utf-8'))
        translations[code] = table
        for key in english:
            value = table.get(key)
            if not isinstance(value, str) or not value.strip():
                problems.append(f'{code}: missing {key!r}')
                continue
            if PRINTF.findall(value) != PRINTF.findall(key):
                problems.append(f'{code}: {key!r} needs the same printf conversions in the same order')
            bad = [] if code in RENDERED else sorted({ch for ch in value if ord(ch) > 126 and ord(ch) not in drawable})
            if bad:
                problems.append(f'{code}: {key!r} uses characters the font lacks: {"".join(bad)}')
            room = budgets.get(key, min(170, int(len(key) * 1.5) + 4))
            if cells(value) > room:
                problems.append(f'{code}: {key!r} is {cells(value)} cells wide; it has room for {room}')
        for key in table:
            if key not in english:
                problems.append(f'{code}: {key!r} is not used any more')

    if args.write:
        import hashlib
        rendered_text, digests, rendered_names = {}, {}, {}
        (ROOT / 'assets' / 'i18n').mkdir(parents=True, exist_ok=True)
        for code, name in LANGUAGES[1:]:
            if code not in RENDERED or code not in translations:
                continue
            renderer = Renderer(code)
            values = {}
            for key, value in translations[code].items():
                values[key], width = renderer.text(value)
                if PRINTF.findall(values[key]) != PRINTF.findall(key):
                    problems.append(f'{code}: {key!r} would pass printf arguments in another order')
                room = budgets.get(key, min(170, int(len(key) * 1.5) + 4))
                if width > room:
                    problems.append(f'{code}: {key!r} renders {width:.1f} cells wide; it has room for {room}')
            rendered_names[code], _ = renderer.text(name)
            blob = renderer.file()
            (ROOT / 'assets' / 'i18n' / f'{code}.runs').write_bytes(blob)
            digests[code] = hashlib.sha256(blob).hexdigest()
            rendered_text[code] = values
        text = generate(translations, rendered_text, digests, rendered_names)
        OUTPUT.write_text(text, encoding='utf-8')
    else:
        current = OUTPUT.read_text(encoding='utf-8') if OUTPUT.exists() else ''
        if f'// inputs {inputs_digest()}' not in current:
            problems.append('src/i18n_strings.inc is out of date (run with --write; rendering needs python3 tools/fetch-ui-fonts.py)')
        import hashlib
        for code in RENDERED & set(translations):
            blob = ROOT / 'assets' / 'i18n' / f'{code}.runs'
            if not blob.exists() or f'// runs {code} {hashlib.sha256(blob.read_bytes()).hexdigest()}' not in current:
                problems.append(f'assets/i18n/{code}.runs does not match the generated tables (run with --write)')

    for problem in problems:
        print(problem, file=sys.stderr)
    print(f'{len(english)} strings, {len(translations)} translations' + (f': {len(problems)} problems' if problems else ': all there'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
