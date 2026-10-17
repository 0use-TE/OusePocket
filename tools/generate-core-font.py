"""Generate LVGL bitmap fonts without a runtime TTF decoder.

Regeneration requires Pillow, plus Python's standard library. The existing
Noto subset is OFL-1.1; rasterized derivatives retain that license. Known UI
copy has 12 pixel sizes, while app names/SSIDs share three 2-bpp CJK fallbacks.
Use --coverage to inspect the source font without changing generated files.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import unicodedata

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/fonts/NotoSansSC-Regular.subset.ttf'
DEST = ROOT / 'components/ouse_core/ouse_font_ui_16.c'
SIZES = [8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 40]
CJK_SIZES = [14, 16, 20]


def unicode_cmap(path):
    """Read Unicode cmap formats 4/12, ignoring .notdef (glyph ID zero).

    Only Unicode-platform and Microsoft Unicode records are considered. This
    small reader deliberately does not parse outlines, shaping, or font names;
    Pillow/FreeType handles rasterization after coverage has been checked.
    """
    data = Path(path).read_bytes()

    def read(fmt, offset, limit=None):
        length = struct.calcsize(fmt)
        if offset < 0 or offset + length > (len(data) if limit is None else limit):
            raise ValueError('Truncated TTF cmap/table')
        return struct.unpack_from(fmt, data, offset)

    table_count, = read('>H', 4)
    cmap_offset = cmap_length = None
    for index in range(table_count):
        tag, _, offset, length = read('>4sIII', 12 + index * 16)
        if offset + length > len(data):
            raise ValueError('TTF table outside file')
        if tag == b'cmap':
            cmap_offset, cmap_length = offset, length
    if cmap_offset is None:
        raise ValueError('Source font has no cmap')
    limit = cmap_offset + cmap_length
    _, record_count = read('>HH', cmap_offset, limit)
    coverage, seen = set(), set()
    for index in range(record_count):
        platform, encoding, relative = read('>HHI', cmap_offset + 4 + index * 8, limit)
        if platform != 0 and not (platform == 3 and encoding in (1, 10)):
            continue
        offset = cmap_offset + relative
        if offset in seen:
            continue
        seen.add(offset)
        fmt, = read('>H', offset, limit)
        if fmt == 12:
            _, _, length, _, group_count = read('>HHIII', offset, limit)
            end = offset + length
            if length < 16 or end > limit or 16 + group_count * 12 > length:
                raise ValueError('Invalid cmap format 12 length')
            for group in range(group_count):
                first, last, glyph = read('>III', offset + 16 + group * 12, end)
                if first > last or last > 0x10ffff:
                    raise ValueError('Invalid cmap format 12 range')
                coverage.update(cp for cp in range(first, last + 1) if glyph + cp - first)
        elif fmt == 4:
            _, length, _, segment_bytes = read('>HHHH', offset, limit)
            end = offset + length
            count = segment_bytes // 2
            if segment_bytes % 2 or length < 16 + count * 8 or end > limit:
                raise ValueError('Invalid cmap format 4 length')
            ends = offset + 14
            starts = ends + count * 2 + 2
            deltas = starts + count * 2
            ranges = deltas + count * 2
            for segment in range(count):
                first, = read('>H', starts + segment * 2, end)
                last, = read('>H', ends + segment * 2, end)
                delta, = read('>h', deltas + segment * 2, end)
                relative, = read('>H', ranges + segment * 2, end)
                if first > last:
                    raise ValueError('Invalid cmap format 4 range')
                for cp in range(first, min(last, 0xfffe) + 1):
                    if relative:
                        glyph, = read('>H', ranges + segment * 2 + relative + (cp - first) * 2, end)
                        if glyph:
                            glyph = (glyph + delta) & 0xffff
                    else:
                        glyph = (cp + delta) & 0xffff
                    if glyph:
                        coverage.add(cp)
    if not coverage:
        raise ValueError('Source font has no supported Unicode cmap')
    return coverage


def gb2312_codepoints():
    result = set()
    for high in range(0xa1, 0xf8):
        for low in range(0xa1, 0xff):
            try:
                result.add(ord(bytes((high, low)).decode('gb2312')))
            except UnicodeDecodeError:
                pass
    return result


def source_copy():
    names = ['ouse_core.c', 'ouse_shell.c', 'ouse_widgets.c', 'ouse_device_ui.c',
             'ouse_dialog.c', 'ouse_screen.c', 'ouse_navigation.cpp', 'ouse_health_ui.c']
    paths = [DEST.parent / name for name in names]
    paths += sorted(path for path in (ROOT / 'apps').rglob('*') if path.suffix in {'.js', '.json'})
    paths += [ROOT / 'components/ouse_runtime/ouse_runtime.c',
              ROOT / 'boards/waveshare_lcd35/ouse_board.c', DEST.parent / 'ui_strings.json']
    return '\n'.join(path.read_text(encoding='utf-8') for path in paths)


def fallback_size(size):
    return 14 if size <= 14 else 16 if size <= 18 else 20


def bounds(font, letters):
    boxes = [font.getbbox(chr(letter), anchor='ls') for letter in letters]
    return boxes, max((-top for _, top, _, _ in boxes), default=0), max((bottom for _, _, _, bottom in boxes), default=0)


def font_metrics(size, ascent, descent):
    baseline = max(round(size * 0.3125), descent)
    return max(round(size * 1.375), ascent + baseline), baseline


def cmap_lines(letters):
    # LVGL sparse offsets/range lengths and glyph IDs are uint16_t. Split
    # Unicode planes/ranges instead of silently overflowing those fields.
    chunks = []
    for letter in letters:
        if not chunks or letter - chunks[-1][0] >= 65535 or len(chunks[-1]) >= 65535:
            chunks.append([])
        chunks[-1].append(letter)
    if len(letters) > 65534:
        raise ValueError('LVGL bitmap font glyph ID exceeds uint16_t')
    lines, entries, glyph_start = [], [], 1
    for index, chunk in enumerate(chunks):
        first = chunk[0]
        lines += [f'static const uint16_t unicode_{index}[] = {{',
                  '    ' + ','.join(str(letter - first) for letter in chunk), '};']
        entries += ['    {', f'        .range_start={first}, .range_length={chunk[-1] - first + 1}, .glyph_id_start={glyph_start},',
                    f'        .unicode_list=unicode_{index}, .list_length={len(chunk)}, .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY', '    },']
        glyph_start += len(chunk)
    return lines + ['static const lv_font_fmt_txt_cmap_t cmaps[] = {', *entries, '};'], len(chunks)


def generate_font(size, letters, bpp, name, fallback, source_sha, minimum_bounds=(0, 0)):
    font = ImageFont.truetype(str(SOURCE), size)
    boxes, ascent, descent = bounds(font, letters)
    line_height, baseline = font_metrics(size, max(ascent, minimum_bounds[0]), max(descent, minimum_bounds[1]))
    bitmap = bytearray()
    glyphs = ['    {0}, // reserved glyph 0']
    pixels_per_byte = 8 // bpp
    maximum = (1 << bpp) - 1
    for letter, (left, top, right, bottom) in zip(letters, boxes):
        char = chr(letter)
        width, height = right - left, bottom - top
        image = Image.new('L', (max(1, width), max(1, height)))
        ImageDraw.Draw(image).text((-left, -top), char, font=font, fill=255, anchor='ls')
        values = [round(value * maximum / 255) for value in image.tobytes()] if width and height else []
        offset = len(bitmap)
        for first in range(0, len(values), pixels_per_byte):
            packed = 0
            for pixel in range(pixels_per_byte):
                value = values[first + pixel] if first + pixel < len(values) else 0
                packed |= value << (8 - (pixel + 1) * bpp)
            bitmap.append(packed)
        glyphs.append(f'    {{.bitmap_index={offset}, .adv_w={round(font.getlength(char) * 16)}, '
                      f'.box_w={width}, .box_h={height}, .ofs_x={left}, .ofs_y={-bottom}}}, // U+{letter:04X}')
    if len(bitmap) > 0xfffff:
        raise ValueError(f'{name}: bitmap exceeds LV_FONT_FMT_TXT_LARGE=0 limit')
    cmaps, cmap_count = cmap_lines(letters)
    lines = ['// SPDX-License-Identifier: OFL-1.1',
             '// Ouse UI bitmap subset derived from Noto Sans SC; see FONT-LICENSE.txt.',
             '// Generated by tools/generate-core-font.py; do not edit.',
             '// Source SHA256: ' + source_sha,
             '#include "lvgl.h"']
    if isinstance(fallback, int):
        lines += [f'LV_FONT_DECLARE(ouse_font_ui_cjk_{fallback})']
    lines += ['', 'static const uint8_t bitmap[] = {']
    lines += ['    ' + ','.join(f'0x{value:02x}' for value in bitmap[first:first + 24]) + ','
              for first in range(0, len(bitmap), 24)]
    lines += ['};', 'static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {', *glyphs, '};',
              *cmaps, 'static const lv_font_fmt_txt_dsc_t font_data = {',
              '    .glyph_bitmap=bitmap, .glyph_dsc=glyphs, .cmaps=cmaps,',
              f'    .cmap_num={cmap_count}, .bpp={bpp}, .bitmap_format=0',
              '};', f'const lv_font_t {name} = {{',
              '    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,',
              '    .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,',
              f'    .line_height={line_height}, .base_line={baseline}, .subpx=LV_FONT_SUBPX_NONE,',
              '    .underline_position=-2, .underline_thickness=1, .dsc=&font_data,']
    if isinstance(fallback, int):
        lines += [f'    .fallback=&ouse_font_ui_cjk_{fallback},']
    else:
        # Retain the previous built-in fallback for glyphs absent from the
        # licensed source subset, including existing LVGL-supported glyphs.
        lines += ['#if LV_FONT_SOURCE_HAN_SANS_SC_14_CJK',
                  '    .fallback=&lv_font_source_han_sans_sc_14_cjk,', '#endif']
    lines += ['};', '']
    target = DEST.with_name(name + '.c')
    target.write_text('\n'.join(lines), encoding='utf-8')
    print(f'Generated {len(letters)} glyphs, {bpp}bpp, {len(bitmap)} bitmap bytes: {target.relative_to(ROOT)}')
    return len(bitmap) + (len(letters) + 1) * 8 + len(letters) * 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--coverage', action='store_true', help='print coverage and fallback estimates; write nothing')
    args = parser.parse_args()
    coverage = unicode_cmap(SOURCE)
    letters = sorted(set(range(32, 127)) | {ord(char) for char in re.findall(r'[^\x00-\x7f]', source_copy())})
    missing = set(letters) - coverage
    cjk_letters = sorted(cp for cp in coverage if cp >= 127 and not unicodedata.category(chr(cp)).startswith('C'))
    cjk_bounds, estimates = {}, {}
    for size in CJK_SIZES:
        boxes, ascent, descent = bounds(ImageFont.truetype(str(SOURCE), size), cjk_letters)
        cjk_bounds[size] = (ascent, descent)
        estimates[size] = {'glyphs': len(cjk_letters), 'bitmap_bytes': sum(((right - left) * (bottom - top) + 3) // 4 for left, top, right, bottom in boxes),
                           'table_bytes': (len(cjk_letters) + 1) * 8 + len(cjk_letters) * 2,
                           'max_ascent': ascent, 'max_descent': descent}
    if args.coverage:
        gb = gb2312_codepoints()
        print(json.dumps({'source_bytes': SOURCE.stat().st_size, 'unicode_glyphs': len(coverage),
                          'ui_glyphs': len(letters),
                          'cjk_ideographs': sum(0x3400 <= cp <= 0x9fff for cp in coverage),
                          'gb2312_total': len(gb), 'gb2312_present': len(gb & coverage),
                          'gb2312_missing': len(gb - coverage), 'fallbacks': estimates,
                          'missing_ui_codepoints': [f'U+{cp:04X}' for cp in sorted(missing)]}, indent=2))
        return
    if missing:
        raise ValueError('Missing source glyphs: ' + ', '.join(f'U+{cp:04X}' for cp in sorted(missing)))
    fallback_estimate = sum(item['bitmap_bytes'] + item['table_bytes'] for item in estimates.values())
    if fallback_estimate > 1_500_000:
        raise ValueError(f'Shared CJK bitmap/table budget exceeded: {fallback_estimate} bytes')
    strings = json.loads((DEST.parent / 'ui_strings.json').read_text(encoding='utf-8'))
    (DEST.parent / 'ui_strings.h').write_text('// Generated from ui_strings.json by generate-core-font.py.\nstatic const struct { const char *zh, *en; } ui_strings[] = {\n'
        + ''.join('{' + json.dumps(zh, ensure_ascii=False) + ',' + json.dumps(en) + '},\n' for zh, en in strings.items()) + '};\n', encoding='utf-8')
    (DEST.parent / 'ui_font_pool.h').write_text('// Generated shared font pool.\n'
        + ''.join(f'LV_FONT_DECLARE(ouse_font_ui_{size})\n' for size in SIZES)
        + 'static const unsigned sizes[]={' + ','.join(map(str, SIZES)) + '};\n'
        + 'static const lv_font_t *const fonts[]={' + ','.join(f'&ouse_font_ui_{size}' for size in SIZES) + '};\n', encoding='utf-8')
    source_sha = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    fallback_bytes = sum(generate_font(size, cjk_letters, 2, f'ouse_font_ui_cjk_{size}', None, source_sha) for size in CJK_SIZES)
    if fallback_bytes > 1_500_000:
        raise ValueError(f'Shared CJK bitmap/table budget exceeded: {fallback_bytes} bytes')
    for size in SIZES:
        selected = fallback_size(size)
        generate_font(size, letters, 4, f'ouse_font_ui_{size}', selected, source_sha, cjk_bounds[selected])
    print(f'Shared CJK bitmap/table total: {fallback_bytes} bytes; source subset is not a complete Chinese font')


if __name__ == '__main__':
    main()
