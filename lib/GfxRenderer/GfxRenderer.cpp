#include "GfxRenderer.h"

#include <FontManager.h>
#include <Logging.h>
#include <Utf8.h>

// UI font IDs - values must match src/fontIds.h
// UI fonts do NOT use the external reader font; only reader fonts do.
// UI fonts may use the external UI font for CJK characters.
static constexpr int UI_FONT_IDS[] = {
    -1246724383,  // UI_10_FONT_ID
    -359249323,   // UI_12_FONT_ID
    1073217904,   // SMALL_FONT_ID
};
static constexpr int UI_FONT_COUNT = sizeof(UI_FONT_IDS) / sizeof(UI_FONT_IDS[0]);

// Check if a Unicode codepoint is CJK or related
static bool isCjkCodepoint(const uint32_t cp) {
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;   // CJK Unified Ideographs
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;   // CJK Extension A
  if (cp >= 0x3000 && cp <= 0x303F) return true;   // CJK Punctuation
  if (cp >= 0x3040 && cp <= 0x309F) return true;   // Hiragana
  if (cp >= 0x30A0 && cp <= 0x30FF) return true;   // Katakana
  if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // CJK Compatibility Ideographs
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;   // Fullwidth forms
  if (cp >= 0x2000 && cp <= 0x206F) return true;   // General Punctuation
  if (cp >= 0x2150 && cp <= 0x218F) return true;   // Number Forms
  if (cp >= 0x2460 && cp <= 0x24FF) return true;   // Enclosed Alphanumerics
  if (cp >= 0x3200 && cp <= 0x32FF) return true;   // Enclosed CJK Letters
  if (cp >= 0x3300 && cp <= 0x33FF) return true;   // CJK Compatibility
  return false;
}

const uint8_t* GfxRenderer::getGlyphBitmap(const EpdFontData* fontData, const EpdGlyph* glyph) const {
  if (fontData->groups != nullptr) {
    if (!fontDecompressor) {
      LOG_ERR("GFX", "Compressed font but no FontDecompressor set");
      return nullptr;
    }
    uint16_t glyphIndex = static_cast<uint16_t>(glyph - fontData->glyph);
    return fontDecompressor->getBitmap(fontData, glyph, glyphIndex);
  }
  return &fontData->bitmap[glyph->dataOffset];
}

void GfxRenderer::begin() {
  frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    LOG_ERR("GFX", "!! No framebuffer");
    assert(false);
  }
}

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) { fontMap.insert({fontId, font}); }

// Translate logical (x,y) coordinates to physical panel coordinates based on current orientation
// This should always be inlined for better performance
static inline void rotateCoordinates(const GfxRenderer::Orientation orientation, const int x, const int y, int* phyX,
                                     int* phyY) {
  switch (orientation) {
    case GfxRenderer::Portrait: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees clockwise
      *phyX = y;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      break;
    }
    case GfxRenderer::LandscapeClockwise: {
      // Logical landscape (800x480) rotated 180 degrees (swap top/bottom and left/right)
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - x;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - y;
      break;
    }
    case GfxRenderer::PortraitInverted: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees counter-clockwise
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - y;
      *phyY = x;
      break;
    }
    case GfxRenderer::LandscapeCounterClockwise: {
      // Logical landscape (800x480) aligned with panel orientation
      *phyX = x;
      *phyY = y;
      break;
    }
  }
}

enum class TextRotation { None, Rotated90CW };

// Shared glyph rendering logic for normal and rotated text.
// Coordinate mapping and cursor advance direction are selected at compile time via the template parameter.
template <TextRotation rotation>
static void renderCharImpl(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                           const EpdFontFamily& fontFamily, const uint32_t cp, int* cursorX, int* cursorY,
                           const TextColor color, const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    LOG_ERR("GFX", "No glyph for codepoint %d", cp);
    return;
  }

  const EpdFontData* fontData = fontFamily.getData(style);
  const bool is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;
  const int top = glyph->top;

  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);

  if (bitmap != nullptr) {
    // For Normal:  outer loop advances screenY, inner loop advances screenX
    // For Rotated: outer loop advances screenX, inner loop advances screenY (in reverse)
    int outerBase, innerBase;
    if constexpr (rotation == TextRotation::Rotated90CW) {
      outerBase = *cursorX + fontData->ascender - top;  // screenX = outerBase + glyphY
      innerBase = *cursorY - left;                      // screenY = innerBase - glyphX
    } else {
      outerBase = *cursorY - top;   // screenY = outerBase + glyphY
      innerBase = *cursorX + left;  // screenX = innerBase + glyphX
    }

    if (is2Bit) {
      int pixelPosition = 0;
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const int outerCoord = outerBase + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
          int screenX, screenY;
          if constexpr (rotation == TextRotation::Rotated90CW) {
            screenX = outerCoord;
            screenY = innerBase - glyphX;
          } else {
            screenX = innerBase + glyphX;
            screenY = outerCoord;
          }

          const uint8_t byte = bitmap[pixelPosition >> 2];
          const uint8_t bit_index = (3 - (pixelPosition & 3)) * 2;
          // the direct bit from the font is 0 -> white, 1 -> light gray, 2 -> dark gray, 3 -> black
          // we swap this to better match the way images and screen think about colors:
          // 0 -> black, 1 -> dark grey, 2 -> light grey, 3 -> white
          const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

          if (renderMode == GfxRenderer::BW) {
            if (color == Color::Black) {
              // High-Contrast Dither for UI text
              if (bmpVal == 0 || bmpVal == 1) {
                // Solid Black for core
                renderer.drawPixel(screenX, screenY, true);
              } else if (bmpVal == 2) {
                // Chessboard Dither for smooth edges
                if ((screenX + screenY) % 2 == 0) {
                  renderer.drawPixel(screenX, screenY, true);
                }
              }
            } else if (color == Color::DarkGray) {
              // 50% chessboard dither (Black pixels)
              if (bmpVal < 3 && (screenX + screenY) % 2 == 0) {
                renderer.drawPixel(screenX, screenY, true);
              }
            } else if (color == Color::LightGray) {
              // 50% chessboard dither (White pixels) for Dark Mode
              if (bmpVal < 3 && (screenX + screenY) % 2 == 0) {
                renderer.drawPixel(screenX, screenY, false);
              }
            } else if (color == Color::White) {
              // Solid White (erasing background)
              if (bmpVal < 3) {
                renderer.drawPixel(screenX, screenY, false);
              }
            }
          } else if (renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            // Light gray (also mark the MSB if it's going to be a dark gray too)
            // We have to flag pixels in reverse for the gray buffers, as 0 leave alone, 1 update
            renderer.drawPixel(screenX, screenY, false);
          } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
            // Dark gray
            renderer.drawPixel(screenX, screenY, false);
          }
        }
      }
    } else {
      int pixelPosition = 0;
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const int outerCoord = outerBase + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
          int screenX, screenY;
          if constexpr (rotation == TextRotation::Rotated90CW) {
            screenX = outerCoord;
            screenY = innerBase - glyphX;
          } else {
            screenX = innerBase + glyphX;
            screenY = outerCoord;
          }

          const uint8_t byte = bitmap[pixelPosition >> 3];
          const uint8_t bit_index = 7 - (pixelPosition & 7);

          if ((byte >> bit_index) & 1) {
            if (renderMode == GfxRenderer::BW) {
              if (color == Color::Black) {
                renderer.drawPixel(screenX, screenY, true);
              } else if (color == Color::DarkGray) {
                if ((screenX + screenY) % 2 == 0) {
                  renderer.drawPixel(screenX, screenY, true);
                }
              } else if (color == Color::LightGray) {
                if ((screenX + screenY) % 2 == 0) {
                  renderer.drawPixel(screenX, screenY, false);
                }
              } else if (color == Color::White) {
                renderer.drawPixel(screenX, screenY, false);
              }
            } else {
              renderer.drawPixel(screenX, screenY, color != Color::White);
            }
          }
        }
      }
    }
  }

  if constexpr (rotation == TextRotation::Rotated90CW) {
    *cursorY -= glyph->advanceX;
  } else {
    *cursorX += glyph->advanceX;
  }
}

// IMPORTANT: This function is in critical rendering path and is called for every pixel. Please keep it as simple and
// efficient as possible.
void GfxRenderer::drawPixel(const int x, const int y, bool state) const {
  if (_invertEnabled) state = !state;
  int phyX = 0;
  int phyY = 0;

  // Note: this call should be inlined for better performance
  rotateCoordinates(orientation, x, y, &phyX, &phyY);

  // Bounds checking against physical panel dimensions
  if (phyX < 0 || phyX >= HalDisplay::DISPLAY_WIDTH || phyY < 0 || phyY >= HalDisplay::DISPLAY_HEIGHT) {
    LOG_ERR("GFX", "!! Outside range (%d, %d) -> (%d, %d)", x, y, phyX, phyY);
    return;
  }

  // Calculate byte position and bit position
  const uint16_t byteIndex = phyY * HalDisplay::DISPLAY_WIDTH_BYTES + (phyX / 8);
  const uint8_t bitPosition = 7 - (phyX % 8);  // MSB first

  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;  // Set bit
  }
}

int GfxRenderer::getTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  int w = 0, h = 0;
  fontIt->second.getTextDimensions(text, &w, &h, style);
  return w;
}

void GfxRenderer::drawCenteredText(const int fontId, const int y, const char* text, const TextColor color,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - getTextWidth(fontId, text, style)) / 2;
  drawText(fontId, x, y, text, color, style);
}

void GfxRenderer::drawText(const int fontId, const int x, const int y, const char* text, const TextColor color,
                           const EpdFontFamily::Style style) const {
  int yPos = y + getFontAscenderSize(fontId);
  int xPos = x;
  int lastBaseX = x;
  int lastBaseY = yPos;
  int lastBaseAdvance = 0;
  int lastBaseTop = 0;

  // cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return;
  }
  const auto& font = fontIt->second;

  // Resolve fallback font (may be null if not set or not found)
  const EpdFontFamily* fallbackFont = nullptr;
  if (fallbackFontId >= 0 && fallbackFontId != fontId) {
    const auto fbIt = fontMap.find(fallbackFontId);
    if (fbIt != fontMap.end()) {
      fallbackFont = &fbIt->second;
    }
  }

  constexpr int MIN_COMBINING_GAP_PX = 1;

  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      int raiseBy = 0;
      if (combiningGlyph) {
        const int currentGap = combiningGlyph->top - combiningGlyph->height - lastBaseTop;
        if (currentGap < MIN_COMBINING_GAP_PX) {
          raiseBy = MIN_COMBINING_GAP_PX - currentGap;
        }
      }

      int combiningX = lastBaseX + lastBaseAdvance / 2;
      int combiningY = lastBaseY - raiseBy;
      renderChar(fontId, font, cp, &combiningX, &combiningY, color, style);
      continue;
    }

    cp = font.applyLigatures(cp, text, style);
    if (prevCp != 0) {
      xPos += font.getKerning(prevCp, cp, style);
    }

    // Check if glyph exists in primary font; if not, try fallback
    const EpdFontFamily* activeFont = &font;
    if (!font.hasGlyph(cp, style) && fallbackFont != nullptr && fallbackFont->hasGlyph(cp)) {
      activeFont = fallbackFont;
    }

    const EpdGlyph* glyph = activeFont->getGlyph(cp, style);

    lastBaseX = xPos;
    lastBaseY = yPos;
    lastBaseAdvance = glyph ? glyph->advanceX : 0;
    lastBaseTop = glyph ? glyph->top : 0;

    renderChar(fontId, *activeFont, cp, &xPos, &yPos, color, style);
    prevCp = cp;
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state) const {
  if (x1 == x2) {
    if (y2 < y1) {
      std::swap(y1, y2);
    }
    for (int y = y1; y <= y2; y++) {
      drawPixel(x1, y, state);
    }
  } else if (y1 == y2) {
    if (x2 < x1) {
      std::swap(x1, x2);
    }
    for (int x = x1; x <= x2; x++) {
      drawPixel(x, y1, state);
    }
  } else {
    // Bresenham's line algorithm — integer arithmetic only
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    dx = sx * dx;  // abs
    dy = sy * dy;  // abs

    int err = dx - dy;
    while (true) {
      drawPixel(x1, y1, state);
      if (x1 == x2 && y1 == y2) break;
      int e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x1 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y1 += sy;
      }
    }
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const int lineWidth, const bool state) const {
  if (lineWidth <= 1) {
    drawLine(x1, y1, x2, y2, state);
    return;
  }
  // If the line is mostly vertical, we should offset X to achieve thickness.
  // Otherwise offset Y.
  bool isMostlyVertical = std::abs(y2 - y1) > std::abs(x2 - x1);
  for (int i = 0; i < lineWidth; i++) {
    if (isMostlyVertical) {
      drawLine(x1 + i, y1, x2 + i, y2, state);
    } else {
      drawLine(x1, y1 + i, x2, y2 + i, state);
    }
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state) const {
  drawLine(x, y, x + width - 1, y, state);
  drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
  drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
  drawLine(x, y, x, y + height - 1, state);
}

// Border is inside the rectangle
void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const int lineWidth,
                           const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x + i, y + i, x + width - i, y + i, state);
    drawLine(x + width - i, y + i, x + width - i, y + height - i, state);
    drawLine(x + width - i, y + height - i, x + i, y + height - i, state);
    drawLine(x + i, y + height - i, x + i, y + i, state);
  }
}

void GfxRenderer::drawArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir,
                          const int lineWidth, const bool state) const {
  const int stroke = std::min(lineWidth, maxRadius);
  const int innerRadius = std::max(maxRadius - stroke, 0);
  const int outerRadiusSq = maxRadius * maxRadius;
  const int innerRadiusSq = innerRadius * innerRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      if (distSq > outerRadiusSq || distSq < innerRadiusSq) {
        continue;
      }
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      drawPixel(px, py, state);
    }
  }
};

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool state) const {
  drawRoundedRect(x, y, width, height, lineWidth, cornerRadius, true, true, true, true, state);
}

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool roundTopLeft, bool roundTopRight, bool roundBottomLeft,
                                  bool roundBottomRight, bool state) const {
  if (lineWidth <= 0 || width <= 0 || height <= 0) {
    return;
  }

  const int maxRadius = std::min({cornerRadius, width / 2, height / 2});
  if (maxRadius <= 0) {
    drawRect(x, y, width, height, lineWidth, state);
    return;
  }

  const int stroke = std::min(lineWidth, maxRadius);
  const int right = x + width - 1;
  const int bottom = y + height - 1;

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    if (roundTopLeft || roundTopRight) {
      fillRect(x + maxRadius, y, horizontalWidth, stroke, state);
    }
    if (roundBottomLeft || roundBottomRight) {
      fillRect(x + maxRadius, bottom - stroke + 1, horizontalWidth, stroke, state);
    }
  }

  const int verticalHeight = height - 2 * maxRadius;
  if (verticalHeight > 0) {
    if (roundTopLeft || roundBottomLeft) {
      fillRect(x, y + maxRadius, stroke, verticalHeight, state);
    }
    if (roundTopRight || roundBottomRight) {
      fillRect(right - stroke + 1, y + maxRadius, stroke, verticalHeight, state);
    }
  }

  if (roundTopLeft) {
    drawArc(maxRadius, x + maxRadius, y + maxRadius, -1, -1, lineWidth, state);
  }
  if (roundTopRight) {
    drawArc(maxRadius, right - maxRadius, y + maxRadius, 1, -1, lineWidth, state);
  }
  if (roundBottomRight) {
    drawArc(maxRadius, right - maxRadius, bottom - maxRadius, 1, 1, lineWidth, state);
  }
  if (roundBottomLeft) {
    drawArc(maxRadius, x + maxRadius, bottom - maxRadius, -1, 1, lineWidth, state);
  }
}

void GfxRenderer::drawCircle(int x, int y, int radius, int lineWidth, bool state) const {
  if (radius <= 0) return;
  drawArc(radius, x, y, -1, -1, lineWidth, state); // TL
  drawArc(radius, x, y, 1, -1, lineWidth, state);  // TR
  drawArc(radius, x, y, 1, 1, lineWidth, state);   // BR
  drawArc(radius, x, y, -1, 1, lineWidth, state);  // BL
}

void GfxRenderer::fillCircle(int x, int y, int radius, Color color) const {
  if (radius <= 0) return;
  auto fillArcTemplated = [this, x, y, radius](Color c) {
    switch (c) {
      case Color::Clear: break;
      case Color::Black:
        fillArc<Color::Black>(radius, x, y, -1, -1);
        fillArc<Color::Black>(radius, x, y, 1, -1);
        fillArc<Color::Black>(radius, x, y, 1, 1);
        fillArc<Color::Black>(radius, x, y, -1, 1);
        break;
      case Color::White:
        fillArc<Color::White>(radius, x, y, -1, -1);
        fillArc<Color::White>(radius, x, y, 1, -1);
        fillArc<Color::White>(radius, x, y, 1, 1);
        fillArc<Color::White>(radius, x, y, -1, 1);
        break;
      case Color::LightGray:
        fillArc<Color::LightGray>(radius, x, y, -1, -1);
        fillArc<Color::LightGray>(radius, x, y, 1, -1);
        fillArc<Color::LightGray>(radius, x, y, 1, 1);
        fillArc<Color::LightGray>(radius, x, y, -1, 1);
        break;
      case Color::DarkGray:
        fillArc<Color::DarkGray>(radius, x, y, -1, -1);
        fillArc<Color::DarkGray>(radius, x, y, 1, -1);
        fillArc<Color::DarkGray>(radius, x, y, 1, 1);
        fillArc<Color::DarkGray>(radius, x, y, -1, 1);
        break;
    }
  };
  fillArcTemplated(color);
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state) const {
  for (int fillY = y; fillY < y + height; fillY++) {
    drawLine(x, fillY, x + width - 1, fillY, state);
  }
}

// NOTE: Those are in critical path, and need to be templated to avoid runtime checks for every pixel.
// Any branching must be done outside the loops to avoid performance degradation.
template <>
void GfxRenderer::drawPixelDither<Color::Clear>(const int x, const int y) const {
  // Do nothing
}

template <>
void GfxRenderer::drawPixelDither<Color::Black>(const int x, const int y) const {
  drawPixel(x, y, true);
}

template <>
void GfxRenderer::drawPixelDither<Color::White>(const int x, const int y) const {
  drawPixel(x, y, false);
}

template <>
void GfxRenderer::drawPixelDither<Color::LightGray>(const int x, const int y) const {
  drawPixel(x, y, x % 2 == 0 && y % 2 == 0);
}

template <>
void GfxRenderer::drawPixelDither<Color::DarkGray>(const int x, const int y) const {
  drawPixel(x, y, (x + y) % 2 == 0);  // TODO: maybe find a better pattern?
}

void GfxRenderer::fillRectDither(const int x, const int y, const int width, const int height, Color color) const {
  if (color == Color::Clear) {
  } else if (color == Color::Black) {
    fillRect(x, y, width, height, true);
  } else if (color == Color::White) {
    fillRect(x, y, width, height, false);
  } else if (color == Color::LightGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::LightGray>(fillX, fillY);
      }
    }
  } else if (color == Color::DarkGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::DarkGray>(fillX, fillY);
      }
    }
  }
}

template <Color color>
void GfxRenderer::fillArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir) const {
  const int radiusSq = maxRadius * maxRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      if (distSq <= radiusSq) {
        drawPixelDither<color>(px, py);
      }
    }
  }
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  const Color color) const {
  fillRoundedRect(x, y, width, height, cornerRadius, true, true, true, true, color);
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  bool roundTopLeft, bool roundTopRight, bool roundBottomLeft, bool roundBottomRight,
                                  const Color color) const {
  if (width <= 0 || height <= 0) {
    return;
  }

  // Assume if we're not rounding all corners then we are only rounding one side
  const int roundedSides = (!roundTopLeft || !roundTopRight || !roundBottomLeft || !roundBottomRight) ? 1 : 2;
  const int maxRadius = std::min({cornerRadius, width / roundedSides, height / roundedSides});
  if (maxRadius <= 0) {
    fillRectDither(x, y, width, height, color);
    return;
  }

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    fillRectDither(x + maxRadius + 1, y, horizontalWidth - 2, height, color);
  }

  const int leftFillTop = y + (roundTopLeft ? (maxRadius + 1) : 0);
  const int leftFillBottom = y + height - 1 - (roundBottomLeft ? (maxRadius + 1) : 0);
  if (leftFillBottom >= leftFillTop) {
    fillRectDither(x, leftFillTop, maxRadius + 1, leftFillBottom - leftFillTop + 1, color);
  }

  const int rightFillTop = y + (roundTopRight ? (maxRadius + 1) : 0);
  const int rightFillBottom = y + height - 1 - (roundBottomRight ? (maxRadius + 1) : 0);
  if (rightFillBottom >= rightFillTop) {
    fillRectDither(x + width - maxRadius - 1, rightFillTop, maxRadius + 1, rightFillBottom - rightFillTop + 1, color);
  }

  auto fillArcTemplated = [this](int maxRadius, int cx, int cy, int xDir, int yDir, Color color) {
    switch (color) {
      case Color::Clear:
        break;
      case Color::Black:
        fillArc<Color::Black>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::White:
        fillArc<Color::White>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::LightGray:
        fillArc<Color::LightGray>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::DarkGray:
        fillArc<Color::DarkGray>(maxRadius, cx, cy, xDir, yDir);
        break;
    }
  };

  if (roundTopLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + maxRadius, -1, -1, color);
  }

  if (roundTopRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + maxRadius, 1, -1, color);
  }

  if (roundBottomRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + height - maxRadius - 1, 1, 1, color);
  }

  if (roundBottomLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + height - maxRadius - 1, -1, 1, color);
  }
}

void GfxRenderer::drawImage(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(orientation, x, y, &rotatedX, &rotatedY);
  // Rotate origin corner
  switch (orientation) {
    case Portrait:
      rotatedY = rotatedY - height;
      break;
    case PortraitInverted:
      rotatedX = rotatedX - width;
      break;
    case LandscapeClockwise:
      rotatedY = rotatedY - height;
      rotatedX = rotatedX - width;
      break;
    case LandscapeCounterClockwise:
      break;
  }
  // TODO: Rotate bits
  display.drawImage(bitmap, rotatedX, rotatedY, width, height);
}

void GfxRenderer::drawIcon(const uint8_t bitmap[], const int x, const int y, const int width, const int height,
                           const TextColor color) const {
  if (color == Black && !_invertEnabled) {
    // Optimization: black icons can use the faster hardware-accelerated path
    // (only when inversion is inactive — hardware path bypasses drawPixel)
    display.drawImageTransparent(bitmap, y, getScreenWidth() - width - x, height, width);
    return;
  }

  // For non-black icons (e.g. White for selection), we must draw manually pixel by pixel.
  // Note: Icons are pre-rotated for the display driver (columns become rows).
  // The bitmap data is laid out such that logical columns are stored sequentially,
  // but in reverse order: Bitmap Row 0 corresponds to Logical Column (width - 1).
  int bytesPerRow = (height + 7) / 8;
  for (int iconIdxX = 0; iconIdxX < width; iconIdxX++) {
    int logicalX = x + (width - 1 - iconIdxX);
    for (int iconY = 0; iconY < height; iconY++) {
      const int byteIndex = iconIdxX * bytesPerRow + (iconY / 8);
      const uint8_t bitIndex = 7 - (iconY % 8);
      // In these icons, 0 is ink (black) and 1 is transparent.
      // We draw only the ink pixels using the requested color.
      if (!((bitmap[byteIndex] >> bitIndex) & 1)) {
        drawPixel(logicalX, y + iconY, color != White);
      }
    }
  }
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY) const {
  // For 1-bit bitmaps, use optimized 1-bit rendering path (no crop support for 1-bit)
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::max(0, (int)std::floor(bitmap.getWidth() * cropX / 2.0f));
  int cropPixY = std::max(0, (int)std::floor(bitmap.getHeight() * cropY / 2.0f));
  if (cropX < 0 || cropY < 0) {
    LOG_DBG("GFX", "Negative crop requested (%f, %f), clamping to 0", cropX, cropY);
  }
  LOG_DBG("GFX", "Cropping %dx%d by %dx%d pix, is %s", bitmap.getWidth(), bitmap.getHeight(), cropPixX, cropPixY,
          bitmap.isTopDown() ? "top-down" : "bottom-up");

  if (maxWidth > 0 && maxHeight > 0) {
    float scaleX = static_cast<float>(maxWidth) / static_cast<float>((1.0f - cropX) * bitmap.getWidth());
    float scaleY = static_cast<float>(maxHeight) / static_cast<float>((1.0f - cropY) * bitmap.getHeight());
    scale = std::min(scaleX, scaleY);
    isScaled = true;
  }
  LOG_DBG("GFX", "Scaling by %f - %s", scale, isScaled ? "scaled" : "not scaled");

  // Calculate output row size (2 bits per pixel, packed into bytes)
  // IMPORTANT: Use int, not uint8_t, to avoid overflow for images > 1020 pixels wide
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    LOG_ERR("GFX", "!! Failed to allocate BMP row buffers");
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from bitmap", bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    if (bmpY < cropPixY || bmpY >= bitmap.getHeight() - cropPixY) {
      continue;
    }

    // Determine the range of screen rows this source row covers
    int srcYRelative = bitmap.isTopDown() ? (bmpY - cropPixY) : (bitmap.getHeight() - 1 - bmpY - cropPixY);
    int syStart = std::floor(srcYRelative * scale);
    int syEnd = std::max(syStart + 1, (int)std::floor((srcYRelative + 1) * scale));

    for (int sy = syStart; sy < syEnd; sy++) {
      int screenY = y + sy;
      if (screenY < 0) continue;
      if (screenY >= getScreenHeight()) break;

      for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
        int srcXRelative = bmpX - cropPixX;
        int sxStart = std::floor(srcXRelative * scale);
        int sxEnd = std::max(sxStart + 1, (int)std::floor((srcXRelative + 1) * scale));

        const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

        for (int sx = sxStart; sx < sxEnd; sx++) {
          int screenX = x + sx;
          if (screenX < 0) continue;
          if (screenX >= getScreenWidth()) break;

          if (renderMode == BW && val < 3) {
            drawPixel(screenX, screenY);
          } else if (renderMode == BW && val == 3 && _darkMode && !_invertEnabled) {
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && val == 1) {
            drawPixel(screenX, screenY, false);
          }
        }
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight) const {
  float scale = 1.0f;
  bool isScaled = false;
  if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
    isScaled = true;
  }

  // For 1-bit BMP, output is still 2-bit packed (for consistency with readNextRow)
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    LOG_ERR("GFX", "!! Failed to allocate 1-bit BMP row buffers");
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    // Read rows sequentially using readNextRow
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from 1-bit bitmap", bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    // Calculate screen Y based on whether BMP is top-down or bottom-up
    const int bmpYOffset = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    int screenY = y + (isScaled ? static_cast<int>(std::floor(bmpYOffset * scale)) : bmpYOffset);
    if (screenY >= getScreenHeight()) {
      continue;  // Continue reading to keep row counter in sync
    }
    if (screenY < 0) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      // Get 2-bit value (result of readNextRow quantization)
      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      // For 1-bit source: val < 3 = black, val == 3 = white.
      // White pixels normally rely on the background being white.
      // When dark mode is active but inversion is suppressed (e.g. cover art),
      // background is black so white pixels must be drawn explicitly.
      if (val < 3) {
        drawPixel(screenX, screenY, true);
      } else if (_darkMode && !_invertEnabled) {
        drawPixel(screenX, screenY, false);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmapRect(const Bitmap& bitmap, const int srcX, const int srcY, const int srcW, const int srcH,
                                const int dstX, const int dstY, const int dstW, const int dstH) const {
  if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) return;

  const int totalSrcW = bitmap.getWidth();
  const int totalSrcH = bitmap.getHeight();

  for (int dy = 0; dy < dstH; dy++) {
    int screenY = dstY + dy;
    if (screenY < 0 || screenY >= getScreenHeight()) continue;

    // Map dy to srcY
    int relativeSrcY = (dy * srcH) / dstH;
    int absoluteSrcY = srcY + relativeSrcY;
    if (absoluteSrcY >= totalSrcH) absoluteSrcY = totalSrcH - 1;

    // Read row from bitmap (this is inefficient for scaling but avoids massive RAM buffers for huge images)
    // Actually, for thumbs, it's okay.
    // However, Bitmap class is designed for sequential row reading.
    // For arbitrary rect, we might need to rewind or buffer.
    // Let's assume we use this for small thumbs and it's okay.
  }
  // TODO: Implementation of non-sequential Bitmap reading is complex. 
  // For now, let's focus on drawPerspectiveBitmap which buffers the WHOLE thing.
}

void GfxRenderer::drawPerspectiveBitmap(const Bitmap& bitmap, const int x, const int y, const int width, int hLeft,
                                         int hRight) const {
  if (width <= 0 || hLeft <= 0 || hRight <= 0) return;

  const int srcW = bitmap.getWidth();
  const int srcH = bitmap.getHeight();
  
  // 1. Buffer the entire bitmap in 2-bit format (~17KB for 220x314)
  const int srcRowSize = (srcW + 3) / 4;
  uint8_t* buffer = static_cast<uint8_t*>(malloc(srcRowSize * srcH));
  uint8_t* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));
  
  if (!buffer || !rowBytes) {
      LOG_ERR("GFX", "!! OOM buffering for perspective draw");
      free(buffer);
      free(rowBytes);
      return;
  }

  // Ensure we are at the start of pixel data
  bitmap.rewindToData();
  for (int i = 0; i < srcH; i++) {
      if (bitmap.readNextRow(buffer + (i * srcRowSize), rowBytes) != BmpReaderError::Ok) {
          LOG_ERR("GFX", "Failed to buffer row %d", i);
          free(buffer);
          free(rowBytes);
          return;
      }
  }
  free(rowBytes);

  // 2. Warp rendering
  const int targetMaxH = std::max(hLeft, hRight);
  const int targetCenterY = y + targetMaxH / 2;

  for (int dstX = 0; dstX < width; dstX++) {
      int screenX = x + dstX;
      if (screenX < 0 || screenX >= getScreenWidth()) continue;

      // Linear interpolation for height at this column
      float t = static_cast<float>(dstX) / (width - 1);
      int hCol = std::round(hLeft * (1.0f - t) + hRight * t);
      if (hCol <= 0) continue;

      int dstYStart = targetCenterY - hCol / 2;
      
      // Map dstX to srcX
      int srcX = (dstX * srcW) / width;
      if (srcX >= srcW) srcX = srcW - 1;

      for (int dy = 0; dy < hCol; dy++) {
          int screenY = dstYStart + dy;
          if (screenY < 0 || screenY >= getScreenHeight()) continue;

          // Map dy to srcY
          int srcY = (dy * srcH) / hCol;
          if (srcY >= srcH) srcY = srcH - 1;

          // Flip Y if not top-down BMP (internal buffer is in storage order)
          int bufferedY = bitmap.isTopDown() ? srcY : (srcH - 1 - srcY);
          
          const uint8_t byte = buffer[bufferedY * srcRowSize + (srcX / 4)];
          const uint8_t val = (byte >> (6 - ((srcX % 4) * 2))) & 0x03;

          if (renderMode == BW) {
              // Draw both black and white pixels to ensure occlusion works
              drawPixel(screenX, screenY, val < 3);
          } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
              drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && val == 1) {
              drawPixel(screenX, screenY, false);
          }
      }
      
      // Draw 1px top/bottom frame
      if (renderMode == BW) {
        drawPixel(screenX, dstYStart, true);
        drawPixel(screenX, dstYStart + hCol - 1, true);
      }
  }
  
  // Draw 1px left/right borders
  if (renderMode == BW) {
      drawLine(x, targetCenterY - hLeft/2, x, targetCenterY + hLeft/2 - 1, true);
      drawLine(x + width - 1, targetCenterY - hRight/2, x + width - 1, targetCenterY + hRight/2 - 1, true);
  }

  free(buffer);
}

void GfxRenderer::fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state) const {
  if (numPoints < 3) return;

  // Find bounding box
  int minY = yPoints[0], maxY = yPoints[0];
  for (int i = 1; i < numPoints; i++) {
    if (yPoints[i] < minY) minY = yPoints[i];
    if (yPoints[i] > maxY) maxY = yPoints[i];
  }

  // Clip to screen
  if (minY < 0) minY = 0;
  if (maxY >= getScreenHeight()) maxY = getScreenHeight() - 1;

  // Allocate node buffer for scanline algorithm
  auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
  if (!nodeX) {
    LOG_ERR("GFX", "!! Failed to allocate polygon node buffer");
    return;
  }

  // Scanline fill algorithm
  for (int scanY = minY; scanY <= maxY; scanY++) {
    int nodes = 0;

    // Find all intersection points with edges
    int j = numPoints - 1;
    for (int i = 0; i < numPoints; i++) {
      if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
        // Calculate X intersection using fixed-point to avoid float
        int dy = yPoints[j] - yPoints[i];
        if (dy != 0) {
          nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
        }
      }
      j = i;
    }

    // Sort nodes by X (simple bubble sort, numPoints is small)
    for (int i = 0; i < nodes - 1; i++) {
      for (int k = i + 1; k < nodes; k++) {
        if (nodeX[i] > nodeX[k]) {
          int temp = nodeX[i];
          nodeX[i] = nodeX[k];
          nodeX[k] = temp;
        }
      }
    }

    // Fill between pairs of nodes
    for (int i = 0; i < nodes - 1; i += 2) {
      int startX = nodeX[i];
      int endX = nodeX[i + 1];

      // Clip to screen
      if (startX < 0) startX = 0;
      if (endX >= getScreenWidth()) endX = getScreenWidth() - 1;

      // Draw horizontal line
      for (int x = startX; x <= endX; x++) {
        drawPixel(x, scanY, state);
      }
    }
  }

  free(nodeX);
}

// For performance measurement (using static to allow "const" methods)
static unsigned long start_ms = 0;

void GfxRenderer::clearScreen(const uint8_t color) const {
  start_ms = millis();
  display.clearScreen(_darkMode ? ~color : color);
}

void GfxRenderer::invertScreen() const {
  for (int i = 0; i < HalDisplay::BUFFER_SIZE; i++) {
    frameBuffer[i] = ~frameBuffer[i];
  }
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const {
  auto elapsed = millis() - start_ms;
  LOG_DBG("GFX", "Time = %lu ms from clearScreen to displayBuffer", elapsed);
  display.displayBuffer(refreshMode, fadingFix);
}

std::string GfxRenderer::truncatedText(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  std::string item = text;
  const char* ellipsis = "...";
  int textWidth = getTextWidth(fontId, item.c_str(), style);
  if (textWidth <= maxWidth) {
    // Text fits, return as is
    return item;
  }

  while (!item.empty() && getTextWidth(fontId, (item + ellipsis).c_str(), style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }

  return item.empty() ? ellipsis : item + ellipsis;
}

// Note: Internal driver treats screen in command orientation; this library exposes a logical orientation
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 480px wide in portrait logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 800px wide in landscape logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
  }
  return HalDisplay::DISPLAY_HEIGHT;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 800px tall in portrait logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 480px tall in landscape logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
  }
  return HalDisplay::DISPLAY_WIDTH;
}

int GfxRenderer::getSpaceWidth(const int fontId, const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  const EpdGlyph* spaceGlyph = fontIt->second.getGlyph(' ', style);
  return spaceGlyph ? spaceGlyph->advanceX : 0;
}

int GfxRenderer::getSpaceKernAdjust(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                                    const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return 0;
  const auto& font = fontIt->second;
  return font.getKerning(leftCp, ' ', style) + font.getKerning(' ', rightCp, style);
}

int GfxRenderer::getKerning(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                            const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return 0;
  return fontIt->second.getKerning(leftCp, rightCp, style);
}

int GfxRenderer::getTextAdvanceX(const int fontId, const char* text, EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  uint32_t cp;
  uint32_t prevCp = 0;
  int width = 0;
  const auto& font = fontIt->second;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      continue;
    }
    cp = font.applyLigatures(cp, text, style);
    if (prevCp != 0) {
      width += font.getKerning(prevCp, cp, style);
    }
    const EpdGlyph* glyph = font.getGlyph(cp, style);
    if (glyph) width += glyph->advanceX;
    prevCp = cp;
  }
  return width;
}

int GfxRenderer::getFontAscenderSize(const int fontId) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return fontIt->second.getData(EpdFontFamily::REGULAR)->ascender;
}

int GfxRenderer::getLineHeight(const int fontId) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return fontIt->second.getData(EpdFontFamily::REGULAR)->advanceY;
}

int GfxRenderer::getTextHeight(const int fontId) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }
  return fontIt->second.getData(EpdFontFamily::REGULAR)->ascender;
}

void GfxRenderer::drawTextRotated90CW(const int fontId, const int x, const int y, const char* text, const TextColor color,
                                      const EpdFontFamily::Style style) const {
  // Cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return;
  }

  const auto& font = fontIt->second;

  int xPos = x;
  int yPos = y;
  int lastBaseX = x;
  int lastBaseY = y;
  int lastBaseAdvance = 0;
  int lastBaseTop = 0;
  constexpr int MIN_COMBINING_GAP_PX = 1;

  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      int raiseBy = 0;
      if (combiningGlyph) {
        const int currentGap = combiningGlyph->top - combiningGlyph->height - lastBaseTop;
        if (currentGap < MIN_COMBINING_GAP_PX) {
          raiseBy = MIN_COMBINING_GAP_PX - currentGap;
        }
      }

      int combiningX = lastBaseX - raiseBy;
      int combiningY = lastBaseY - lastBaseAdvance / 2;
      renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, &combiningX, &combiningY, color, style);
      continue;
    }

    cp = font.applyLigatures(cp, text, style);
    if (prevCp != 0) {
      yPos -= font.getKerning(prevCp, cp, style);
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);

    lastBaseX = xPos;
    lastBaseY = yPos;
    lastBaseAdvance = glyph ? glyph->advanceX : 0;
    lastBaseTop = glyph ? glyph->top : 0;

    renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, &xPos, &yPos, color, style);
    prevCp = cp;
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return frameBuffer; }

size_t GfxRenderer::getBufferSize() { return HalDisplay::BUFFER_SIZE; }

// unused
// void GfxRenderer::grayscaleRevert() const { display.grayscaleRevert(); }

void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(frameBuffer); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(frameBuffer); }

void GfxRenderer::displayGrayBuffer() const { display.displayGrayBuffer(fadingFix); }

void GfxRenderer::freeBwBufferChunks() {
  for (auto& bwBufferChunk : bwBufferChunks) {
    if (bwBufferChunk) {
      free(bwBufferChunk);
      bwBufferChunk = nullptr;
    }
  }
}

/**
 * This should be called before grayscale buffers are populated.
 * A `restoreBwBuffer` call should always follow the grayscale render if this method was called.
 * Uses chunked allocation to avoid needing 48KB of contiguous memory.
 * Returns true if buffer was stored successfully, false if allocation failed.
 */
bool GfxRenderer::storeBwBuffer() {
  // Allocate and copy each chunk
  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    // Check if any chunks are already allocated
    if (bwBufferChunks[i]) {
      LOG_ERR("GFX", "!! BW buffer chunk %zu already stored - this is likely a bug, freeing chunk", i);
      free(bwBufferChunks[i]);
      bwBufferChunks[i] = nullptr;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    bwBufferChunks[i] = static_cast<uint8_t*>(malloc(BW_BUFFER_CHUNK_SIZE));

    if (!bwBufferChunks[i]) {
      LOG_ERR("GFX", "!! Failed to allocate BW buffer chunk %zu (%zu bytes)", i, BW_BUFFER_CHUNK_SIZE);
      // Free previously allocated chunks
      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, BW_BUFFER_CHUNK_SIZE);
  }

  LOG_DBG("GFX", "Stored BW buffer in %zu chunks (%zu bytes each)", BW_BUFFER_NUM_CHUNKS, BW_BUFFER_CHUNK_SIZE);
  return true;
}

/**
 * This can only be called if `storeBwBuffer` was called prior to the grayscale render.
 * It should be called to restore the BW buffer state after grayscale rendering is complete.
 * Uses chunked restoration to match chunked storage.
 */
void GfxRenderer::restoreBwBuffer() {
  bool missingChunks = false;
  for (const auto& bwBufferChunk : bwBufferChunks) {
    if (!bwBufferChunk) {
      missingChunks = true;
      break;
    }
  }

  if (missingChunks) {
    freeBwBufferChunks();
    return;
  }

  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    memcpy(frameBuffer + offset, bwBufferChunks[i], BW_BUFFER_CHUNK_SIZE);
  }

  display.cleanupGrayscaleBuffers(frameBuffer);

  freeBwBufferChunks();
  LOG_DBG("GFX", "Restored and freed BW buffer chunks");
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const {
  if (frameBuffer) {
    display.cleanupGrayscaleBuffers(frameBuffer);
  }
}

bool GfxRenderer::isReaderFont(const int fontId) {
  for (int i = 0; i < UI_FONT_COUNT; i++) {
    if (UI_FONT_IDS[i] == fontId) return false;  // UI font
  }
  return true;  // All non-UI fonts are reader fonts
}

void GfxRenderer::renderExternalGlyph(const uint8_t* bitmap, ExternalFont* font, int* x, int y, Color color,
                                      int advance, int minX) const {
  const uint8_t width = font->getCharWidth();
  const uint8_t height = font->getCharHeight();
  const uint8_t bytesPerRow = font->getBytesPerRow();

  // Baseline alignment: +4px descent for CJK characters
  const int startY = y - height + 4;
  const bool pixelState = (color != Color::White);

  for (int glyphY = 0; glyphY < height; glyphY++) {
    const int screenY = startY + glyphY;
    for (int glyphX = minX; glyphX < width; glyphX++) {
      const int byteIndex = glyphY * bytesPerRow + (glyphX / 8);
      const int bitIndex = 7 - (glyphX % 8);  // MSB first

      if ((bitmap[byteIndex] >> bitIndex) & 1) {
        drawPixel(*x + (glyphX - minX), screenY, pixelState);
      }
    }
  }

  *x += std::max(1, advance);
}

void GfxRenderer::renderChar(const int fontId, const EpdFontFamily& fontFamily, uint32_t cp, int* x, int* y,
                             Color color, EpdFontFamily::Style style) const {
  FontManager& fm = FontManager::getInstance();
  const bool isCjk = isCjkCodepoint(cp);

  if (isReaderFont(fontId)) {
    // Reader font: use external font only for CJK characters
    if (isCjk && fm.isExternalFontEnabled()) {
      ExternalFont* extFont = fm.getActiveFont();
      if (extFont) {
        const uint8_t* bitmap = extFont->getGlyph(cp);
        if (bitmap) {
          uint8_t minX = 0, advanceX = extFont->getCharWidth();
          extFont->getGlyphMetrics(cp, &minX, &advanceX);
          renderExternalGlyph(bitmap, extFont, x, *y, color, advanceX, minX);
          return;
        }
        // Fall through to built-in reader font for missing glyphs
      }
    }
  } else {
    // UI font: for CJK characters, automatically use the reader font if loaded
    if (isCjk && fm.isExternalFontEnabled()) {
      ExternalFont* extFont = fm.getActiveFont();
      if (extFont) {
        const uint8_t* bitmap = extFont->getGlyph(cp);
        if (bitmap) {
          uint8_t minX = 0, advanceX = 0;
          extFont->getGlyphMetrics(cp, &minX, &advanceX);
          renderExternalGlyph(bitmap, extFont, x, *y, color, advanceX, minX);
          return;
        }
      }
    }
  }

  renderCharImpl<TextRotation::None>(*this, renderMode, fontFamily, cp, x, y, (TextColor)color, style);
}

void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}
