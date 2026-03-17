/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <HalStorage.h>
#line 11
#include <Logging.h>

namespace {
inline void write16(FsFile& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(FsFile& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(FsFile& out, const int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}
}  // namespace

bool Xtc::load() {
  LOG_DBG("XTC", "Loading XTC: %s", filepath.c_str());

  // Initialize parser
  parser.reset(new xtc::XtcParser());

  // Open XTC file
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Failed to load: %s", xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  LOG_DBG("XTC", "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("XTC", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("XTC", "Failed to clear cache");
    return false;
  }

  LOG_DBG("XTC", "Cache cleared successfully");
  return true;
}

void Xtc::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      Storage.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  Storage.mkdir(cachePath.c_str());
}

size_t Xtc::getFileSize() const {
  FsFile f = Storage.open(filepath.c_str());
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get author from XTC metadata
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() const {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  // Already generated
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
    return false;
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  // Get bit depth
  const uint8_t bitDepth = parser->getBitDepth();
  const uint16_t pageWidth = parser->getWidth();
  const uint16_t pageHeight = parser->getHeight();

  // Allocate buffer for page data
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageWidth + 7) / 8) * pageHeight;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    LOG_ERR("XTC", "Failed to allocate page buffer (%lu bytes)", bitmapSize);
    return false;
  }

  // Load first page (cover)
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR("XTC", "Failed to load cover page");
    free(pageBuffer);
    return false;
  }

  // Create BMP file
  FsFile coverBmp;
  if (!Storage.openFileForWrite("XTC", getCoverBmpPath(), coverBmp)) {
    LOG_DBG("XTC", "Failed to create cover BMP file");
    free(pageBuffer);
    return false;
  }

  // Write BMP header
  // BMP file header (14 bytes)
  const uint32_t rowSize = ((pageWidth + 31) / 32) * 4;  // Row size aligned to 4 bytes
  const uint32_t imageSize = rowSize * pageHeight;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;  // Header + DIB + palette + data

  // File header
  coverBmp.write('B');
  coverBmp.write('M');
  coverBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;  // 1-bit palette has 2 colors (8 bytes)
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  // DIB header (BITMAPINFOHEADER - 40 bytes)
  uint32_t dibHeaderSize = 40;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t width = pageWidth;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&width), 4);
  int32_t height = -static_cast<int32_t>(pageHeight);  // Negative for top-down
  coverBmp.write(reinterpret_cast<const uint8_t*>(&height), 4);
  uint16_t planes = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;  // 1-bit monochrome
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;  // BI_RGB (no compression)
  coverBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;  // 72 DPI
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  // Color palette (2 colors for 1-bit)
  // XTC 1-bit polarity: 0 = black, 1 = white (standard BMP palette order)
  // Color 0: Black (text/foreground in XTC)
  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};
  coverBmp.write(black, 4);
  // Color 1: White (background in XTC)
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};
  coverBmp.write(white, 4);

  // Write bitmap data
  // BMP requires 4-byte row alignment
  const size_t dstRowSize = (pageWidth + 7) / 8;  // 1-bit destination row size

  if (bitDepth == 2) {
    // XTH 2-bit mode: Two bit planes, column-major order
    // - Columns scanned right to left (x = width-1 down to 0)
    // - 8 vertical pixels per byte (MSB = topmost pixel in group)
    // - First plane: Bit1, Second plane: Bit2
    // - Pixel value = (bit1 << 1) | bit2
    const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
    const uint8_t* plane1 = pageBuffer;                 // Bit1 plane
    const uint8_t* plane2 = pageBuffer + planeSize;     // Bit2 plane
    const size_t colBytes = (pageHeight + 7) / 8;  // Bytes per column

    // Allocate a row buffer for 1-bit output
    uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(dstRowSize));
    if (!rowBuffer) {
      free(pageBuffer);
      coverBmp.close();
      return false;
    }

    for (uint16_t y = 0; y < pageHeight; y++) {
      memset(rowBuffer, 0xFF, dstRowSize);  // Start with all white

      for (uint16_t x = 0; x < pageWidth; x++) {
        // Column-major, right to left: column index = (width - 1 - x)
        const size_t colIndex = pageWidth - 1 - x;
        const size_t byteInCol = y / 8;
        const size_t bitInByte = 7 - (y % 8);  // MSB = topmost pixel

        const size_t byteOffset = colIndex * colBytes + byteInCol;
        const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
        const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
        const uint8_t pixelValue = (bit1 << 1) | bit2;

        // Threshold: 0=white (1); 1,2,3=black (0)
        if (pixelValue >= 1) {
          // Set bit to 0 (black) in BMP format
          const size_t dstByte = x / 8;
          const size_t dstBit = 7 - (x % 8);
          rowBuffer[dstByte] &= ~(1 << dstBit);
        }
      }

      // Write converted row
      coverBmp.write(rowBuffer, dstRowSize);

      // Pad to 4-byte boundary
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - dstRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }

    free(rowBuffer);
  } else {
    // 1-bit source: write directly with proper padding
    const size_t srcRowSize = (pageWidth + 7) / 8;

    for (uint16_t y = 0; y < pageHeight; y++) {
      // Write source row
      coverBmp.write(pageBuffer + y * srcRowSize, srcRowSize);

      // Pad to 4-byte boundary
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - srcRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }
  }

  coverBmp.close();
  free(pageBuffer);

  LOG_DBG("XTC", "Generated cover BMP: %s", getCoverBmpPath().c_str());
  return true;
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Xtc::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Xtc::generateThumbBmp(int height) const {
  // Already generated
  if (Storage.exists(getThumbBmpPath(height).c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate thumb BMP, file not loaded");
    return false;
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  // Get bit depth
  const uint8_t bitDepth = parser->getBitDepth();
  const uint16_t pageWidth = parser->getWidth();
  const uint16_t pageHeight = parser->getHeight();

  // Allocate buffer for page data
  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageWidth + 7) / 8) * pageHeight;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    LOG_INF("XTC", "Page buffer too large (%lu bytes), using streaming thumbnail", bitmapSize);
    return generateThumbBmpStreaming(height);
  }

#line 372
  // Load first page (cover)
  if (const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize) == 0) {
    LOG_ERR("XTC", "Failed to load cover page for thumbnail");
    free(pageBuffer);
    return false;
  }

  // Calculate target dimensions for thumbnail (fit within 3x3 grid or Home card)
  // 25-kai book ratio (14.8×21cm ≈ 0.7)
  const int THUMB_TARGET_WIDTH = height * 220 / 320;
  const int THUMB_TARGET_HEIGHT = height;

  // Detect content bounding box to remove white margins
  // Use a density threshold to ignore scanner noise at edges
  const size_t bpcSize = (bitDepth == 2) ? ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) : 0;
  const uint8_t* bpcPlane1 = (bitDepth == 2) ? pageBuffer : nullptr;
  const uint8_t* bpcPlane2 = (bitDepth == 2) ? pageBuffer + bpcSize : nullptr;
  const size_t bpcColBytes = (bitDepth == 2) ? ((pageHeight + 7) / 8) : 0;
  const size_t bpcSrcRowBytes = (bitDepth == 1) ? ((pageWidth + 7) / 8) : 0;

  uint16_t contentXStart = pageWidth, contentXEnd = 0;
  uint16_t contentYStart = pageHeight, contentYEnd = 0;

  // Phase 1: Robust content detection (horizontal)
  for (uint16_t x = 0; x < pageWidth; x++) {
    int blackCount = 0;
    for (uint16_t y = 0; y < pageHeight; y++) {
      bool isBlack = false;
      if (bitDepth == 2) {
        const size_t colIndex = pageWidth - 1 - x;
        const size_t byteOffset = colIndex * bpcColBytes + (y / 8);
        if (((bpcPlane1[byteOffset] >> (7 - (y % 8))) & 1) || ((bpcPlane2[byteOffset] >> (7 - (y % 8))) & 1))
          isBlack = true;
      } else {
        if (!((pageBuffer[y * bpcSrcRowBytes + x / 8] >> (7 - (x % 8))) & 1)) isBlack = true;
      }
      if (isBlack) blackCount++;
    }
    // Require at least 0.5% density or 3 pixels to count as content (ignores noise lines)
    if (blackCount > std::max(2, static_cast<int>(pageHeight / 200))) {
      if (x < contentXStart) contentXStart = x;
      if (x > contentXEnd) contentXEnd = x;
    }
  }

  // Phase 2: Robust content detection (vertical)
  for (uint16_t y = 0; y < pageHeight; y++) {
    int blackCount = 0;
    for (uint16_t x = 0; x < pageWidth; x++) {
      bool isBlack = false;
      if (bitDepth == 2) {
        const size_t colIndex = pageWidth - 1 - x;
        const size_t byteOffset = colIndex * bpcColBytes + (y / 8);
        if (((bpcPlane1[byteOffset] >> (7 - (y % 8))) & 1) || ((bpcPlane2[byteOffset] >> (7 - (y % 8))) & 1))
          isBlack = true;
      } else {
        if (!((pageBuffer[y * bpcSrcRowBytes + x / 8] >> (7 - (x % 8))) & 1)) isBlack = true;
      }
      if (isBlack) blackCount++;
    }
    if (blackCount > std::max(2, static_cast<int>(pageWidth / 200))) {
      if (y < contentYStart) contentYStart = y;
      if (y > contentYEnd) contentYEnd = y;
    }
  }

  // Fallback if page is entirely white or detection failed
  if (contentXStart >= contentXEnd || contentYStart >= contentYEnd) {
    contentXStart = 0; contentXEnd = pageWidth - 1;
    contentYStart = 0; contentYEnd = pageHeight - 1;
  }
  
  // Safety margin (2px)
  contentXStart = (contentXStart > 2) ? contentXStart - 2 : 0;
  contentYStart = (contentYStart > 2) ? contentYStart - 2 : 0;
  contentXEnd = (contentXEnd + 2 < pageWidth) ? contentXEnd + 2 : pageWidth - 1;
  contentYEnd = (contentYEnd + 2 < pageHeight) ? contentYEnd + 2 : pageHeight - 1;

  uint16_t contentWidth = contentXEnd - contentXStart + 1;
  uint16_t contentHeight = contentYEnd - contentYStart + 1;

  // Calculate scaling to fit content into target thumb size while preserving aspect ratio
  float scaleX = static_cast<float>(THUMB_TARGET_WIDTH) / contentWidth;
  float scaleY = static_cast<float>(THUMB_TARGET_HEIGHT) / contentHeight;
  float scale = std::max(scaleX, scaleY);

  uint16_t scaledWidth = static_cast<uint16_t>(contentWidth * scale);
  uint16_t scaledHeight = static_cast<uint16_t>(contentHeight * scale);
  
  // Center content in fixed 0.7 ratio box
  int32_t offsetX = (THUMB_TARGET_WIDTH - scaledWidth) / 2;
  int32_t offsetY = (THUMB_TARGET_HEIGHT - scaledHeight) / 2;

  LOG_DBG("XTC", "Thumb (Flow 220:320 ratio): %dx%d -> %dx%d (scaled %dx%d, offset %d,%d)", 
          contentWidth, contentHeight, THUMB_TARGET_WIDTH, THUMB_TARGET_HEIGHT, scaledWidth, scaledHeight, offsetX, offsetY);

  // Create thumbnail BMP file
  FsFile thumbBmp;
  if (!Storage.openFileForWrite("XTC", getThumbBmpPath(height), thumbBmp)) {
    LOG_DBG("XTC", "Failed to create thumb BMP file");
    free(pageBuffer);
    return false;
  }

  // BMP row sizing (aligned to 4 bytes)
  const uint32_t rowSize = (THUMB_TARGET_WIDTH + 31) / 32 * 4;
  const uint32_t imageSize = rowSize * THUMB_TARGET_HEIGHT;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;

  // BMP Headers
  thumbBmp.write('B'); thumbBmp.write('M');
  write32(thumbBmp, fileSize);
  write32(thumbBmp, 0); // reserved
  write32(thumbBmp, 62); // offset to pixels (14 + 40 + 8)

  write32(thumbBmp, 40); // DIB size
  write32Signed(thumbBmp, THUMB_TARGET_WIDTH);
  write32Signed(thumbBmp, -THUMB_TARGET_HEIGHT); // top-down
  write16(thumbBmp, 1); // planes
  write16(thumbBmp, 1); // bpp
  write32(thumbBmp, 0); // compression
  write32(thumbBmp, imageSize);
  write32(thumbBmp, 2835); // 72 DPI
  write32(thumbBmp, 2835);
  write32(thumbBmp, 2); // colors used
  write32(thumbBmp, 2); // important colors

  // Palette (Index 0 = Black, Index 1 = White)
  uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  thumbBmp.write(palette, 8);

  uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(rowSize));
  if (!rowBuffer) {
    free(pageBuffer);
    thumbBmp.close();
    return false;
  }

  uint32_t scaleInv_fp = static_cast<uint32_t>(65536.0f / scale);

  for (int32_t dstY = 0; dstY < THUMB_TARGET_HEIGHT; dstY++) {
    // Fill row with white
    memset(rowBuffer, 0xFF, rowSize);

    // Process all destination pixels. OffsetX/Y may be negative due to "Fill" scale.
    uint32_t srcYOffset = dstY - offsetY;
    int32_t srcYStart_fixed = (int32_t)contentYStart + ((srcYOffset * (int32_t)scaleInv_fp) >> 16);
    int32_t srcYEnd_fixed = (int32_t)contentYStart + (((srcYOffset + 1) * (int32_t)scaleInv_fp) >> 16);

    // Only process if this source row range is within page bounds
    if (srcYEnd_fixed > 0 && srcYStart_fixed < (int32_t)pageHeight) {
      uint32_t srcYStart = std::max((int32_t)0, srcYStart_fixed);
      uint32_t srcYEnd = std::min((uint32_t)pageHeight, (uint32_t)std::max((int32_t)0, srcYEnd_fixed));
      
      for (int32_t dstX = 0; dstX < THUMB_TARGET_WIDTH; dstX++) {
        uint32_t srcXOffset = dstX - offsetX;
        int32_t srcXStart_fixed = (int32_t)contentXStart + ((srcXOffset * (int32_t)scaleInv_fp) >> 16);
        int32_t srcXEnd_fixed = (int32_t)contentXStart + (((srcXOffset + 1) * (int32_t)scaleInv_fp) >> 16);

        // Skip if outside source bounds (for robustness, though offsetX/offsetY usually handle this)
        if (srcXEnd_fixed <= 0 || srcXStart_fixed >= (int32_t)pageWidth) continue;

        uint32_t srcXStart = std::max((int32_t)0, srcXStart_fixed);
        uint32_t srcXEnd = std::min((uint32_t)pageWidth, (uint32_t)std::max((int32_t)0, srcXEnd_fixed));

        uint32_t graySum = 0, totalCount = 0;
        for (uint32_t srcY = srcYStart; srcY < srcYEnd && srcY < pageHeight; srcY++) {
          for (uint32_t srcX = srcXStart; srcX < srcXEnd && srcX < pageWidth; srcX++) {
            uint8_t grayVal = 255;
            if (bitDepth == 2) {
              const size_t colIndex = pageWidth - 1 - srcX;
              const size_t byteOffset = colIndex * bpcColBytes + (srcY / 8);
              uint8_t b1 = (bpcPlane1[byteOffset] >> (7 - (srcY % 8))) & 1;
              uint8_t b2 = (bpcPlane2[byteOffset] >> (7 - (srcY % 8))) & 1;
              grayVal = (3 - (b1 << 1 | b2)) * 85;
            } else {
              grayVal = ((pageBuffer[srcY * bpcSrcRowBytes + srcX / 8] >> (7 - (srcX % 8))) & 1) ? 255 : 0;
            }
            graySum += grayVal;
            totalCount++;
          }
        }

        uint8_t avgGray = (totalCount > 0) ? static_cast<uint8_t>(graySum / totalCount) : 255;
        if (avgGray < 116) {
          rowBuffer[dstX / 8] &= ~(1 << (7 - (dstX % 8))); // Black
        }
      }
    }
    thumbBmp.write(rowBuffer, rowSize);
  }

  free(rowBuffer);
  thumbBmp.close();
  free(pageBuffer);

  LOG_DBG("XTC", "Generated thumb BMP: %s", getThumbBmpPath(height).c_str());
  return true;
}

// Streaming fallback for large pages that exceed available heap.
// Two-pass approach: Pass 1 detects white-margin bounding box (same
// thresholds as the non-streaming path); Pass 2 renders the thumbnail
// using the detected content region. Peak working memory is ~1 source
// row + colBlacks array (pw * 4 bytes) — well under 8 KB for 480-wide pages.
// Only supports 1-bit XTC — 2-bit BPC uses column-major layout which
// requires random access and cannot be streamed row by row.
bool Xtc::generateThumbBmpStreaming(int height) const {
  const uint8_t  bd  = parser->getBitDepth();
  const uint16_t pw  = parser->getWidth();
  const uint16_t ph  = parser->getHeight();

  if (bd != 1) {
    LOG_ERR("XTC", "Streaming thumbnail not supported for 2-bit XTC");
    return false;
  }

  const int      thumbW      = static_cast<int>(height * 220.0f / 320.0f);
  const int      thumbH      = height;
  const uint32_t srcRowBytes = (static_cast<uint32_t>(pw) + 7) / 8;
  const uint32_t dstRowSize  = (static_cast<uint32_t>(thumbW) + 31) / 32 * 4;

  // One source row buffer, reused across both passes
  std::vector<uint8_t> srcRowBuf(srcRowBytes, 0xFF);

  // ── Pass 1: accumulate column black-pixel counts + Y-bounds ──────────────
  // colBlacks[x] = number of black pixels in column x across all rows.
  // Uses pw * 4 bytes (1920 B for 480-wide pages) — freed before Pass 2.
  std::vector<uint32_t> colBlacks(pw, 0);
  uint32_t contentYStart = ph;   // Initialised to "not found"
  uint32_t contentYEnd   = 0;
  const int yThreshold   = std::max(2, (int)pw / 200);  // Same as non-streaming path
  const int xThreshold   = std::max(2, (int)ph / 200);

  uint32_t accumulated = 0;
  uint32_t curSrcRow   = 0;

  auto processRow1 = [&]() {
    int rowBlacks = 0;
    for (uint16_t x = 0; x < pw; x++) {
      if (!((srcRowBuf[x / 8] >> (7 - x % 8)) & 1)) {
        colBlacks[x]++;
        rowBlacks++;
      }
    }
    if (rowBlacks > yThreshold) {
      if (curSrcRow < contentYStart) contentYStart = curSrcRow;
      contentYEnd = curSrcRow;   // Rows arrive in order; last wins
    }
    curSrcRow++;
    accumulated = 0;
    std::fill(srcRowBuf.begin(), srcRowBuf.end(), 0xFF);
  };

  const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(
      0,
      [&](const uint8_t* data, size_t size, size_t) {
        size_t pos = 0;
        while (pos < size) {
          size_t avail = std::min<uint32_t>(srcRowBytes - accumulated,
                                            static_cast<uint32_t>(size - pos));
          memcpy(srcRowBuf.data() + accumulated, data + pos, avail);
          accumulated += avail;
          pos += avail;
          if (accumulated == srcRowBytes) processRow1();
        }
      },
      4096);

  // ── Compute X bounds from colBlacks, then free it ────────────────────────
  uint32_t contentXStart = pw, contentXEnd = 0;
  for (uint16_t x = 0; x < pw; x++) {
    if ((int)colBlacks[x] > xThreshold) {
      if (x < contentXStart) contentXStart = x;
      contentXEnd = x;
    }
  }
  colBlacks.clear();  // Free ~1920 bytes before opening the output file

  // Fallback if page is entirely white or detection failed
  if (contentXStart >= contentXEnd || contentYStart >= contentYEnd) {
    contentXStart = 0; contentXEnd = pw - 1;
    contentYStart = 0; contentYEnd = ph - 1;
  }

  // Safety margin (2px) — same as non-streaming path
  contentXStart = (contentXStart > 2) ? contentXStart - 2 : 0;
  contentYStart = (contentYStart > 2) ? contentYStart - 2 : 0;
  contentXEnd   = (contentXEnd + 2 < pw) ? contentXEnd + 2 : pw - 1;
  contentYEnd   = (contentYEnd + 2 < ph) ? contentYEnd + 2 : ph - 1;

  const uint32_t contentWidth  = contentXEnd - contentXStart + 1;
  const uint32_t contentHeight = contentYEnd - contentYStart + 1;

  // Scale to fill target box — same max(scaleX, scaleY) logic as non-streaming path
  const float scaleX = (float)thumbW / (float)contentWidth;
  const float scaleY = (float)thumbH / (float)contentHeight;
  const float scale  = std::max(scaleX, scaleY);

  const int32_t scaledWidth  = (int32_t)((float)contentWidth  * scale);
  const int32_t scaledHeight = (int32_t)((float)contentHeight * scale);
  const int32_t offsetX      = (thumbW - scaledWidth)  / 2;
  const int32_t offsetY      = (thumbH - scaledHeight) / 2;
  const uint32_t scaleInv_fp = (uint32_t)(65536.0f / scale);  // Fixed-point 1/scale

  LOG_INF("XTC", "Streaming thumb bbox: x[%lu..%lu] y[%lu..%lu] scale=%.3f offset=(%ld,%ld)",
          contentXStart, contentXEnd, contentYStart, contentYEnd, scale,
          (long)offsetX, (long)offsetY);

  // ── Open BMP output file ──────────────────────────────────────────────────
  setupCacheDir();
  FsFile thumbBmp;
  if (!Storage.openFileForWrite("XTC", getThumbBmpPath(height), thumbBmp)) {
    LOG_ERR("XTC", "Streaming: failed to create thumb BMP file");
    return false;
  }

  // BMP header — identical format to non-streaming path
  const uint32_t imageSize = dstRowSize * static_cast<uint32_t>(thumbH);
  const uint32_t fileSize  = 14 + 40 + 8 + imageSize;
  thumbBmp.write('B'); thumbBmp.write('M');
  write32(thumbBmp, fileSize);
  write32(thumbBmp, 0);
  write32(thumbBmp, 62);
  write32(thumbBmp, 40);
  write32Signed(thumbBmp, thumbW);
  write32Signed(thumbBmp, -thumbH);
  write16(thumbBmp, 1);
  write16(thumbBmp, 1);
  write32(thumbBmp, 0);
  write32(thumbBmp, imageSize);
  write32(thumbBmp, 2835);
  write32(thumbBmp, 2835);
  write32(thumbBmp, 2);
  write32(thumbBmp, 2);
  // Color table: index 0 = black, index 1 = white
  thumbBmp.write((uint8_t)0x00); thumbBmp.write((uint8_t)0x00);
  thumbBmp.write((uint8_t)0x00); thumbBmp.write((uint8_t)0x00);
  thumbBmp.write((uint8_t)0xFF); thumbBmp.write((uint8_t)0xFF);
  thumbBmp.write((uint8_t)0xFF); thumbBmp.write((uint8_t)0x00);

  // ── Pass 2: render thumbnail with content-aware mapping ──────────────────
  std::vector<uint8_t> dstRowBuf(dstRowSize, 0xFF);
  std::fill(srcRowBuf.begin(), srcRowBuf.end(), 0xFF);
  accumulated        = 0;
  curSrcRow          = 0;
  uint32_t nextThumbRow = 0;

  // Called when a complete source row is ready in srcRowBuf (= curSrcRow).
  // Emits all thumbnail rows whose nearest-neighbour source row == curSrcRow.
  auto flushSrcRow2 = [&]() {
    while (nextThumbRow < static_cast<uint32_t>(thumbH)) {
      const int32_t srcYOffset = (int32_t)nextThumbRow - offsetY;
      const int32_t neededSrcY =
          (int32_t)contentYStart + ((srcYOffset * (int32_t)scaleInv_fp) >> 16);

      if (neededSrcY < 0) {
        // Thumb row maps before the page top — white padding
        std::fill(dstRowBuf.begin(), dstRowBuf.end(), 0xFF);
        thumbBmp.write(dstRowBuf.data(), dstRowSize);
        nextThumbRow++;
        continue;
      }
      if (neededSrcY > (int32_t)curSrcRow) break;   // Need a later source row
      if (neededSrcY < (int32_t)curSrcRow) {
        // Source row already passed (only happens at top padding) — white
        std::fill(dstRowBuf.begin(), dstRowBuf.end(), 0xFF);
        thumbBmp.write(dstRowBuf.data(), dstRowSize);
        nextThumbRow++;
        continue;
      }

      // neededSrcY == curSrcRow — render this thumbnail row
      std::fill(dstRowBuf.begin(), dstRowBuf.end(), 0xFF);
      for (int32_t dx = 0; dx < thumbW; dx++) {
        const int32_t srcXOffset = dx - offsetX;
        const int32_t srcX =
            (int32_t)contentXStart + ((srcXOffset * (int32_t)scaleInv_fp) >> 16);
        if (srcX < 0 || srcX >= (int32_t)pw) continue;
        if (!((srcRowBuf[srcX / 8] >> (7 - srcX % 8)) & 1))
          dstRowBuf[dx / 8] &= ~(0x80 >> (dx % 8));  // Black pixel
      }
      thumbBmp.write(dstRowBuf.data(), dstRowSize);
      nextThumbRow++;
    }
    curSrcRow++;
    accumulated = 0;
    std::fill(srcRowBuf.begin(), srcRowBuf.end(), 0xFF);
  };

  xtc::XtcError err = const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(
      0,
      [&](const uint8_t* data, size_t size, size_t) {
        size_t pos = 0;
        while (pos < size) {
          size_t avail = std::min<uint32_t>(srcRowBytes - accumulated,
                                            static_cast<uint32_t>(size - pos));
          memcpy(srcRowBuf.data() + accumulated, data + pos, avail);
          accumulated += avail;
          pos += avail;
          if (accumulated == srcRowBytes) flushSrcRow2();
        }
      },
      4096);

  // Pad any remaining thumbnail rows with white
  std::fill(dstRowBuf.begin(), dstRowBuf.end(), 0xFF);
  while (nextThumbRow < static_cast<uint32_t>(thumbH)) {
    thumbBmp.write(dstRowBuf.data(), dstRowSize);
    nextThumbRow++;
  }

  thumbBmp.close();

  if (err != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Streaming thumbnail failed: %s", xtc::errorToString(err));
    Storage.remove(getThumbBmpPath(height).c_str());
    return false;
  }

  LOG_INF("XTC", "Generated streaming thumb BMP (bbox): %s", getThumbBmpPath(height).c_str());
  return true;
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex,
                                     std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                     size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
