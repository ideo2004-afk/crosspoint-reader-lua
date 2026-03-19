# CJK Font Converter for XTEINK X4

This is a font conversion tool specifically developed for the XTEINK X4 (CrossPoint Reader). It converts TrueType (TTF) or OpenType (OTF) fonts into the `.bin` bitmap format compatible with the system to support custom external fonts.

## Getting Started

1. **Python Environment**: Ensure Python 3 is installed on your computer.
2. **Install Pillow**:
   ```bash
   pip install Pillow
   ```
3. **Add Fonts**: Place the `.ttf` or `.otf` fonts you wish to convert into the `fonts/` directory.

## Creating Font .bin Files

Use the `convert_font.py` script to perform the conversion.

### Basic Command
```bash
python convert_font.py --font fonts/your_font.ttf --size 32
```

### Argument Descriptions
- `--font`: Path to the source font file (Required).
- `--size`: Font size in pt (Required).
- `--measure`: Measure the suggested cell (grid) size without performing actual conversion.
- `--width` / `--height`: Manually specify the cell width/height (Auto-detected if omitted).
- `--threshold`: Binarization threshold (0-255, default 128). Lower = Bolder, Higher = Thinner.
- `--sharpen`: Apply sharpening filter before binarization.
- `--preview`: Generate a `preview.png` in the output directory for visual check.
- `--upscale`: Super-sampling factor (e.g. 4). Renders at higher resolution before downscaling for better quality.
- `--dither`: Use Floyd-Steinberg dithering instead of hard thresholding (Recommended for E-ink).
- `--gamma`: Gamma correction for weight adjustment (default 1.0). Use `> 1.0` for bolder, `< 1.0` for thinner.

### Example: Preview Suggested Cell Size
```bash
python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 32 --measure
```

### Example: Basic Conversion
```bash
python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 32
```

### Example: High-Quality E-ink Optimized (Recommended)
This command uses super-sampling and dithering to achieve smoothness similar to professional layout engines.
```bash
python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 32 --upscale 4 --dither --gamma 1.5 --preview
```
The converted file will be generated in the `output/` directory alongside a `preview.png`.

## Installation to SD Card

To enable custom fonts on your device:

1. **Check Filename**: The generated file must follow the naming convention: `FontName_Size_WxH.bin` (e.g., `TaipeiSansTC_32_32x33.bin`).
2. **Transfer to SD Card**: Copy the generated `.bin` file to the **/fonts/** directory on your SD card.
   - Path: `SD_CARD/fonts/`
3. **Usage**: You can now reference this font name and size directly in your Lua plugins.

## Technical Specifications
- **Range**: Supports Unicode U+0000 to U+FFFF (Basic Multilingual Plane).
- **Format**: 1-bit per pixel (MSB-first), row-major order.
- **Alignment**: The font baseline is positioned 4 pixels from the bottom of the bitmap row.
- **File Size Calculation**: `bytesPerChar = ceil(width/8) * height`; Total file size = `65536 * bytesPerChar`.
