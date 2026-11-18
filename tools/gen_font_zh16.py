"""Generate the SH1106 glyph table from the bundled WenQuanYi BDF."""

import argparse
from pathlib import Path


CELL_SIZE = 16
PUNCTUATION = "，。！？：；、（）《》“”‘’"


def selected_codepoints():
    codepoints = set(map(ord, PUNCTUATION))
    # GB2312 区位 16～87 包含一级和二级汉字。
    for lead in range(0xB0, 0xF8):
        for trail in range(0xA1, 0xFF):
            character = bytes((lead, trail)).decode("gb2312", errors="ignore")
            if character:
                codepoints.add(ord(character))
    return codepoints


def align_bitmap(codepoint, bounds, bitmap, baseline):
    if bounds is None or bitmap is None:
        raise ValueError(f"Incomplete bitmap for U+{codepoint:04X}")

    width, height, left, bottom = bounds
    top = baseline - bottom - height
    if not (0 <= left <= CELL_SIZE - width and 0 <= top <= CELL_SIZE - height):
        raise ValueError(f"U+{codepoint:04X} does not fit a {CELL_SIZE}×{CELL_SIZE} cell")
    if len(bitmap) != height:
        raise ValueError(f"Unexpected bitmap height for U+{codepoint:04X}")

    # BDF 的每行从高位开始，低位补齐到整字节。
    padding = (-width) % 8
    shift = CELL_SIZE - left - width
    rows = [0] * CELL_SIZE
    for row, bits in enumerate(bitmap, start=top):
        rows[row] = (bits >> padding) << shift
    return rows


def read_glyphs(bdf, codepoints):
    glyphs = {}
    baseline = None
    codepoint = None
    bounds = None
    bitmap = None
    with bdf.open(encoding="ascii") as source:
        for line in source:
            field, _, value = line.strip().partition(" ")
            if field == "FONT_ASCENT":
                baseline = int(value)
            elif field == "STARTCHAR":
                codepoint = bounds = bitmap = None
            elif field == "ENCODING":
                codepoint = int(value)
            elif codepoint not in codepoints:
                continue
            elif field == "BBX":
                bounds = tuple(map(int, value.split()))
            elif field == "BITMAP":
                bitmap = []
            elif field == "ENDCHAR":
                glyphs[codepoint] = align_bitmap(codepoint, bounds, bitmap, baseline)
            elif bitmap is not None:
                bitmap.append(int(field, 16))

    missing = codepoints - glyphs.keys()
    if missing:
        preview = ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
        raise ValueError(f"BDF is missing {len(missing)} selected glyphs: {preview}")
    return glyphs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bdf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    glyphs = read_glyphs(args.bdf, selected_codepoints())
    lines = ["/* Generated from WenQuanYi Bitmap Song. See fonts/wenquanyi/NOTICE.txt. */"]
    for codepoint, rows in sorted(glyphs.items()):
        row_data = ", ".join(f"0x{row:04X}" for row in rows)
        lines.append(f"    {{0x{codepoint:04X}, {{{row_data}}}}},")
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
