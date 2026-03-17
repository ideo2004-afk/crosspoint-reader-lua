#!/usr/bin/env python3
"""
Convert a TrueType/OpenType font to CrossPoint External Font .bin format.

Format spec:
  - No header (all metadata encoded in filename)
  - offset = codepoint * bytesPerChar
  - bytesPerChar = ceil(width/8) * height
  - 1-bit per pixel, MSB-first, row-major order
  - Glyph baseline at bitmap row (height - 4)

Filename convention: FontName_size_WxH.bin
  e.g. TaipeiSansTC_30_30x31.bin

Usage:
    python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 30
    python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 30 --width 30 --height 31
    python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 30 --measure
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow not installed. Run: pip install Pillow")
    sys.exit(1)

# Full BMP coverage: U+0000 to U+FFFF
MAX_CODEPOINT = 0xFFFF


def detect_cell_size(font_path: str, pt_size: int):
    """Auto-detect appropriate cell dimensions for the given font and size."""
    font = ImageFont.truetype(font_path, pt_size)
    ascent, descent = font.getmetrics()
    descent = abs(descent)

    # Use a CJK character to measure actual rendered width
    ref_char = '永'
    try:
        bbox = font.getbbox(ref_char)
        cjk_width = bbox[2] - bbox[0] if bbox else pt_size
    except Exception:
        cjk_width = pt_size

    cell_width = cjk_width
    cell_height = ascent + descent

    return cell_width, cell_height, ascent, descent


def render_codepoint(cp: int, font: ImageFont.FreeTypeFont, cell_width: int, cell_height: int) -> bytes:
    """Render a single codepoint directly to 1-bit. Returns all-zero for empty/missing glyphs."""
    bytes_per_row = (cell_width + 7) // 8
    bytes_per_char = bytes_per_row * cell_height

    # Skip non-printable control characters
    if cp < 0x20 or (0x7F <= cp <= 0x9F):
        return bytes(bytes_per_char)

    try:
        char = chr(cp)
        img = Image.new('1', (cell_width, cell_height), 0)
        draw = ImageDraw.Draw(img)
        baseline_y = cell_height - 4
        try:
            draw.text((0, baseline_y), char, font=font, fill=1, anchor="ls")
        except TypeError:
            ascent, _ = font.getmetrics()
            draw.text((0, baseline_y - ascent), char, font=font, fill=1)
    except Exception:
        return bytes(bytes_per_char)

    result = bytearray(bytes_per_char)
    for row in range(cell_height):
        for col in range(cell_width):
            if img.getpixel((col, row)):
                byte_idx = row * bytes_per_row + col // 8
                bit_idx = 7 - (col % 8)
                result[byte_idx] |= 1 << bit_idx

    return bytes(result)


def convert_font(font_path: str, pt_size: int, cell_width: int, cell_height: int, output_path: str):
    font = ImageFont.truetype(font_path, pt_size)

    bytes_per_row = (cell_width + 7) // 8
    bytes_per_char = bytes_per_row * cell_height
    total_codepoints = MAX_CODEPOINT + 1
    total_bytes = total_codepoints * bytes_per_char

    print(f"Font      : {Path(font_path).name}")
    print(f"Size      : {pt_size}pt")
    print(f"Cell      : {cell_width}x{cell_height} px  ({bytes_per_char} bytes/glyph)")
    print(f"Range     : U+0000 to U+{MAX_CODEPOINT:04X} ({total_codepoints:,} codepoints)")
    print(f"Output    : {output_path}")
    print(f"File size : {total_bytes / 1024 / 1024:.1f} MB")
    print()

    with open(output_path, 'wb') as f:
        for cp in range(total_codepoints):
            if cp % 2048 == 0:
                pct = cp * 100 // total_codepoints
                print(f"  U+{cp:04X}  {pct:3d}%", end='\r', flush=True)
            f.write(render_codepoint(cp, font, cell_width, cell_height))

    actual_size = Path(output_path).stat().st_size
    print(f"\nDone. {actual_size / 1024 / 1024:.1f} MB → {output_path}")


def main():
    parser = argparse.ArgumentParser(description='Convert TTF/OTF font to CrossPoint .bin format')
    parser.add_argument('--font', required=True, help='Path to TTF or OTF font file')
    parser.add_argument('--size', type=int, required=True, help='Font size in points')
    parser.add_argument('--width', type=int, help='Cell width in pixels (auto-detected if omitted)')
    parser.add_argument('--height', type=int, help='Cell height in pixels (auto-detected if omitted)')
    parser.add_argument('--output', help='Output .bin path (auto-generated in output/ if omitted)')
    parser.add_argument('--measure', action='store_true', help='Measure cell size and exit without converting')
    args = parser.parse_args()

    font_path = args.font
    if not Path(font_path).exists():
        print(f"Error: font not found: {font_path}")
        sys.exit(1)

    # Detect or use provided cell size
    auto_width, auto_height, ascent, descent = detect_cell_size(font_path, args.size)
    cell_width  = args.width  if args.width  else auto_width
    cell_height = args.height if args.height else auto_height

    if args.measure:
        print(f"Font    : {Path(font_path).name}  @{args.size}pt")
        print(f"Ascent  : {ascent}px")
        print(f"Descent : {descent}px")
        print(f"Auto cell size: {auto_width}x{auto_height}")
        if args.width or args.height:
            print(f"Override cell : {cell_width}x{cell_height}")
        return

    # Generate output filename: FontName_size_WxH.bin
    if args.output:
        output_path = args.output
    else:
        # Use font filename stem directly, just replace spaces with underscores
        stem = Path(font_path).stem.replace(' ', '_')

        output_dir = Path(font_path).parent.parent / 'output'
        output_dir.mkdir(exist_ok=True)
        output_path = str(output_dir / f"{stem}_{args.size}_{cell_width}x{cell_height}.bin")

    convert_font(font_path, args.size, cell_width, cell_height, output_path)


if __name__ == '__main__':
    main()
