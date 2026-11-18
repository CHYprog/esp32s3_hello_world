"""Generate the SH1106 glyph table from the bundled WenQuanYi BDF."""

import argparse
from pathlib import Path


CELL_SIZE = 16
ASCII_CODES = range(0x20, 0x7F)
PUNCTUATION = "，。！？：；、（）《》“”‘’"


def selected_codepoints():
    codepoints = set(ASCII_CODES) | set(map(ord, PUNCTUATION))
    # GB2312 区位 16～87 包含一级和二级汉字。
    for lead in range(0xB0, 0xF8):
        for trail in range(0xA1, 0xFF):
            character = bytes((lead, trail)).decode("gb2312", errors="ignore")
            if character:
                codepoints.add(ord(character))
    return codepoints


def align_bitmap(codepoint, bounds, bitmap, baseline, advance):
    if bounds is None or bitmap is None:
        raise ValueError(f"Incomplete bitmap for U+{codepoint:04X}")

    width, height, left, bottom = bounds
    top = baseline - bottom - height
    cell_width = CELL_SIZE
    if codepoint in ASCII_CODES:
        if advance is None:
            raise ValueError(f"Missing advance for U+{codepoint:04X}")
        # 保留字符自身的宽度和间距。负左边距移入字框，避免覆盖前一个字符。
        cell_width = advance + max(0, -left)
        left = max(0, left)
        # 将带下伸笔画的字形完整放进 16 像素高的区域。
        top = min(top, CELL_SIZE - height)
    if not (0 <= left <= cell_width - width and 0 <= top <= CELL_SIZE - height):
        raise ValueError(f"U+{codepoint:04X} does not fit a {CELL_SIZE}×{CELL_SIZE} cell")
    if not 1 <= cell_width <= CELL_SIZE:
        raise ValueError(f"Unexpected advance for U+{codepoint:04X}")
    if len(bitmap) != height:
        raise ValueError(f"Unexpected bitmap height for U+{codepoint:04X}")

    # BDF 的每行从高位开始，低位补齐到整字节。
    padding = (-width) % 8
    shift = CELL_SIZE - left - width
    rows = [0] * CELL_SIZE
    for row, bits in enumerate(bitmap, start=top):
        rows[row] = (bits >> padding) << shift
    return cell_width, rows


def read_glyphs(bdf, codepoints):
    glyphs = {}
    baseline = None
    codepoint = None
    bounds = None
    bitmap = None
    advance = None
    with bdf.open(encoding="ascii") as source:
        for line in source:
            field, _, value = line.strip().partition(" ")
            if field == "FONT_ASCENT":
                baseline = int(value)
            elif field == "STARTCHAR":
                codepoint = bounds = bitmap = advance = None
            elif field == "ENCODING":
                codepoint = int(value)
            elif codepoint not in codepoints:
                continue
            elif field == "BBX":
                bounds = tuple(map(int, value.split()))
            elif field == "DWIDTH":
                advance = int(value.split()[0])
            elif field == "BITMAP":
                bitmap = []
            elif field == "ENDCHAR":
                glyphs[codepoint] = align_bitmap(codepoint, bounds, bitmap, baseline, advance)
            elif bitmap is not None:
                bitmap.append(int(field, 16))

    missing = codepoints - glyphs.keys()
    if missing:
        preview = ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
        raise ValueError(f"BDF is missing {len(missing)} selected glyphs: {preview}")
    return glyphs


def format_rows(rows):
    return ", ".join(f"0x{row:04X}" for row in rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bdf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    glyphs = read_glyphs(args.bdf, selected_codepoints())
    lines = [
        "/* Generated from WenQuanYi Bitmap Song. See fonts/wenquanyi/NOTICE.txt. */",
        "static const sh1106_ascii_glyph_t font_ascii[] = {",
    ]
    for codepoint in ASCII_CODES:
        width, rows = glyphs[codepoint]
        lines.append(f"    {{{width}, {{{format_rows(rows)}}}}}, /* U+{codepoint:04X} */")
    lines.extend(["};", "", "static const sh1106_zh16_glyph_t font_zh16[] = {"])
    for codepoint in sorted(glyphs.keys() - set(ASCII_CODES)):
        _, rows = glyphs[codepoint]
        lines.append(f"    {{0x{codepoint:04X}, {{{format_rows(rows)}}}}},")
    lines.append("};")
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
