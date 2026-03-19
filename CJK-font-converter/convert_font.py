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
    from PIL import Image, ImageDraw, ImageFont, ImageFilter
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


def render_codepoint(cp: int, font: ImageFont.FreeTypeFont, cell_width: int, cell_height: int,
                     threshold: int = 128, sharpen: bool = False,
                     upscale: int = 1, dither: bool = False, upscale_font: ImageFont.FreeTypeFont = None,
                     gamma: float = 1.0) -> bytes:
    """Render a single codepoint directly to 1-bit. Returns all-zero for empty/missing glyphs."""
    bytes_per_row = (cell_width + 7) // 8
    bytes_per_char = bytes_per_row * cell_height

    # Skip non-printable control characters
    if cp < 0x20 or (0x7F <= cp <= 0x9F):
        return bytes(bytes_per_char)

    try:
        char = chr(cp)
        # 1. Render in 'L' (8-bit grayscale) mode
        curr_width, curr_height = cell_width, cell_height
        curr_font = font
        if upscale > 1 and upscale_font:
            curr_width *= upscale
            curr_height *= upscale
            curr_font = upscale_font

        img = Image.new('L', (curr_width, curr_height), 255) # White background
        draw = ImageDraw.Draw(img)
        baseline_y = curr_height - (4 * upscale if upscale > 1 else 4)
        
        try:
            draw.text((0, baseline_y), char, font=curr_font, fill=0, anchor="ls") # Black ink
        except TypeError:
            ascent, _ = curr_font.getmetrics()
            draw.text((0, baseline_y - ascent), char, font=curr_font, fill=0)

        # 2. Downscale if upscaled
        if upscale > 1:
            # Use Resampling.LANCZOS if available (Pillow 9.1+), else Image.LANCZOS
            resample_filter = getattr(Image, 'Resampling', Image).LANCZOS
            img = img.resize((cell_width, cell_height), resample_filter)

        # 3. Apply Gamma Correction
        if gamma != 1.0:
            # Darken or lighten the anti-aliased grays
            # gamma > 1.0: Darker grays -> Bolder output
            # gamma < 1.0: Lighter grays -> Thinner output
            img = img.point(lambda p: int(255 * (p / 255) ** (1 / gamma)))

        # 4. Apply sharpening if requested
        if sharpen:
            img = img.filter(ImageFilter.SHARPEN)

        # 4. Binarization (Dither or Threshold)
        if dither:
            img = img.convert('1', dither=Image.FLOYDSTEINBERG)
        else:
            # Manual thresholding
            img = img.point(lambda p: 255 if p > threshold else 0).convert('1')
    except Exception:
        return bytes(bytes_per_char)

    result = bytearray(bytes_per_char)
    # img is now 1-bit
    pixels = img.load()
    for row in range(cell_height):
        for col in range(cell_width):
            if not pixels[col, row]: # 0 is Black (Ink) in 1-bit mode if not inverted
                # Actually, in '1' mode, 0 is Black, 255 is White.
                # If we want Black pixels to be bit 1:
                byte_idx = row * bytes_per_row + col // 8
                bit_idx = 7 - (col % 8)
                result[byte_idx] |= 1 << bit_idx

    return bytes(result)


def convert_font(font_path: str, pt_size: int, cell_width: int, cell_height: int, output_path: str,
                 threshold: int = 128, sharpen: bool = False, preview: bool = False,
                 upscale: int = 1, dither: bool = False, gamma: float = 1.0):
    font = ImageFont.truetype(font_path, pt_size)
    upscale_font = None
    if upscale > 1:
        upscale_font = ImageFont.truetype(font_path, pt_size * upscale)

    bytes_per_row = (cell_width + 7) // 8
    bytes_per_char = bytes_per_row * cell_height
    total_codepoints = MAX_CODEPOINT + 1
    total_bytes = total_codepoints * bytes_per_char

    print(f"Font      : {Path(font_path).name}")
    print(f"Size      : {pt_size}pt")
    print(f"Cell      : {cell_width}x{cell_height} px  ({bytes_per_char} bytes/glyph)")
    print(f"Range     : U+0000 to U+{MAX_CODEPOINT:04X} ({total_codepoints:,} codepoints)")
    print(f"Threshold : {threshold} {'(Sharpened)' if sharpen else ''}")
    if upscale > 1:
        print(f"Upscale   : {upscale}x")
    if dither:
        print(f"Dither    : Floyd-Steinberg")
    if gamma != 1.0:
        print(f"Gamma     : {gamma}")
    print(f"Output    : {output_path}")
    print(f"File size : {total_bytes / 1024 / 1024:.1f} MB")
    print()

    # Generate preview if requested
    if preview:
        preview_chars = "永國我ABC123!@#"
        cols = len(preview_chars)
        preview_img = Image.new('1', (cols * cell_width, cell_height), 0)
        for i, char in enumerate(preview_chars):
            glyph_bytes = render_codepoint(ord(char), font, cell_width, cell_height, threshold, sharpen, upscale, dither, upscale_font, gamma)
            # Paste glyph into preview_img
            for row in range(cell_height):
                for col in range(cell_width):
                    byte_idx = row * bytes_per_row + col // 8
                    bit_idx = 7 - (col % 8)
                    if (glyph_bytes[byte_idx] >> bit_idx) & 1:
                        preview_img.putpixel((i * cell_width + col, row), 1)
        preview_fn = str(Path(output_path).with_name("preview.png"))
        preview_img.save(preview_fn)
        print(f"Preview saved to: {preview_fn}")

    with open(output_path, 'wb') as f:
        for cp in range(total_codepoints):
            if cp % 2048 == 0:
                pct = cp * 100 // total_codepoints
                print(f"  U+{cp:04X}  {pct:3d}%", end='\r', flush=True)
            f.write(render_codepoint(cp, font, cell_width, cell_height, threshold, sharpen, upscale, dither, upscale_font, gamma))

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
    parser.add_argument('--threshold', type=int, default=128, help='Binarization threshold (0-255, default 128)')
    parser.add_argument('--sharpen', action='store_true', help='Apply sharpening filter before binarization')
    parser.add_argument('--preview', action='store_true', help='Generate preview.png in the same directory as output')
    parser.add_argument('--upscale', type=int, default=1, help='Upscale factor for super-sampling (e.g. 2 or 4)')
    parser.add_argument('--dither', action='store_true', help='Use Floyd-Steinberg dithering instead of thresholding')
    parser.add_argument('--gamma', type=float, default=1.0, help='Gamma correction for weight adjustment (e.g. 1.5 bolder, 0.7 thinner)')
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

    convert_font(font_path, args.size, cell_width, cell_height, output_path, args.threshold, args.sharpen, args.preview, args.upscale, args.dither, args.gamma)


if __name__ == '__main__':
    main()
