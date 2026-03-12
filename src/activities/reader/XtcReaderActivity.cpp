/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

XtcReaderActivity::~XtcReaderActivity() {
  if (pageBuffer) {
    free(pageBuffer);
    pageBuffer = nullptr;
  }
}

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <algorithm>

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

namespace {
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
}  // namespace

void XtcReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderer.setDarkMode(false);  // Reader manages its own dark mode inversion

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);  // Clear ghosting before reader content

  if (!xtc) {
    return;
  }

  xtc->setupCacheDir();

  // Load saved progress
  loadProgress();
  loadBookmarks();

  // Save current XTC as last opened book and add to recent books
  APP_STATE.openEpubPath = xtc->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(xtc->getPath(), xtc->getTitle(), xtc->getAuthor(), xtc->getThumbBmpPath());
  READING_STATS.recordOpen(xtc->getPath(), xtc->getTitle());

  sessionStartMillis = millis();

  // Trigger first update
  skipNextButtonCheck = true;
  requestUpdate();
}

void XtcReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();
  renderer.setDarkMode(SETTINGS.darkMode);  // Restore renderer dark mode for UI

  if (sessionStartMillis > 0 && xtc) {
    uint32_t elapsedSeconds = (millis() - sessionStartMillis) / 1000;
    READING_STATS.addReadingTime(xtc->getPath(), xtc->getTitle(), elapsedSeconds);
    READING_STATS.saveToFile();
    sessionStartMillis = 0;
  }

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  xtc.reset();
}

void XtcReaderActivity::loop() {
  ActivityWithSubactivity::loop();
  
  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    return;
  }

  // Skip button processing after returning from subactivity
  if (skipNextButtonCheck) {
    const bool confirmReleased = !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                                 !mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    const bool backReleased = !mappedInput.isPressed(MappedInputManager::Button::Back) &&
                              !mappedInput.wasReleased(MappedInputManager::Button::Back);
    if (confirmReleased && backReleased) {
      skipNextButtonCheck = false;
    }
    return;
  }

  const unsigned long longPressMs = 600;

  // === Menu Input Handling ===
  if (inMenu) {
    // --- Inline scrubber ---
    if (inScrubber) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Left))  { scrubberPercent = (scrubberPercent > 0)   ? scrubberPercent - 1  : 0;   requestUpdate(); return; }
      if (mappedInput.wasPressed(MappedInputManager::Button::Right)) { scrubberPercent = (scrubberPercent < 100) ? scrubberPercent + 1  : 100; requestUpdate(); return; }
      if (mappedInput.wasPressed(MappedInputManager::Button::Up))    { scrubberPercent = (scrubberPercent + 10 <= 100) ? scrubberPercent + 10 : 100; requestUpdate(); return; }
      if (mappedInput.wasPressed(MappedInputManager::Button::Down))  { scrubberPercent = (scrubberPercent - 10 >= 0)   ? scrubberPercent - 10 : 0;   requestUpdate(); return; }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        jumpToPercent(scrubberPercent);
        inScrubber = false;
        inMenu = false;
        skipNextButtonCheck = true;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        inScrubber = false; // back to menu
        requestUpdate();
        return;
      }
      return;
    }

    if (mappedInput.wasReleasedRaw(HalGPIO::BTN_UP)) {
      menuSelectedIndex = (menuSelectedIndex > 0) ? menuSelectedIndex - 1 : 4;
      requestUpdate();
    } else if (mappedInput.wasReleasedRaw(HalGPIO::BTN_DOWN)) {
      menuSelectedIndex = (menuSelectedIndex < 4) ? menuSelectedIndex + 1 : 0;
      requestUpdate();
    } else if (mappedInput.wasShortPressed(MappedInputManager::Button::Confirm)) {
      // Execute Menu Action (Right Cluster)
      if (menuSelectedIndex == 0) { // Resume
        inMenu = false;
        requestUpdate();
      } else if (menuSelectedIndex == 1) { // Go to — inline scrubber
        if (xtc) {
          const size_t total = xtc->getPageCount();
          scrubberPercent = total > 0 ? (int)(currentPage * 100 / total) : 0;
          inScrubber = true;
          requestUpdate();
        }
      } else if (menuSelectedIndex == 2) { // Dark Mode Toggle
        inMenu = false;
        SETTINGS.darkMode = !SETTINGS.darkMode;
        SETTINGS.saveToFile();
        requestUpdate();
      } else if (menuSelectedIndex == 3) { // Screenshot
        inMenu = false;
        pendingScreenshot = true;
        requestUpdate();
      } else if (menuSelectedIndex == 4) { // Exit
        inMenu = false;
        mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
        onGoHome();
      }
      return;
    } else if (mappedInput.wasShortPressed(MappedInputManager::Button::Back)) {
      // Resume/Cancel (Left Cluster)
      inMenu = false;
      requestUpdate();
      return;
    } else if (mappedInput.wasLongPressedRaw(HalGPIO::BTN_BACK, longPressMs) ||
               mappedInput.wasLongPressedRaw(HalGPIO::BTN_CONFIRM, longPressMs) ||
               mappedInput.wasLongPressedRaw(HalGPIO::BTN_LEFT, longPressMs) ||
               mappedInput.wasLongPressedRaw(HalGPIO::BTN_RIGHT, longPressMs)) {
      inMenu = false;
      requestUpdate();
      return;
    }
    return;
  }

  // === Custom Fixed Button Layout ===

  // Front LEFT: short=prev, long=home (snappy)
  if (mappedInput.wasLongPressedRaw(HalGPIO::BTN_BACK, 800) || 
      mappedInput.wasLongPressedRaw(HalGPIO::BTN_CONFIRM, 800)) {
    mappedInput.consumeButtonRaw(HalGPIO::BTN_BACK);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
    onGoHome();
    return;
  }
  const bool frontLeftShort = mappedInput.wasShortPressedRaw(HalGPIO::BTN_BACK, 800) || 
                              mappedInput.wasShortPressedRaw(HalGPIO::BTN_CONFIRM, 800);

  // Front RIGHT: short=next, long=menu (snappy)
  if (mappedInput.wasLongPressedRaw(HalGPIO::BTN_LEFT, 600) ||
      mappedInput.wasLongPressedRaw(HalGPIO::BTN_RIGHT, 600)) {
    inMenu = true;
    menuSelectedIndex = 0;
    requestUpdate();
    return;
  }
  const bool frontRightShort = mappedInput.wasShortPressedRaw(HalGPIO::BTN_LEFT, 600) ||
                               mappedInput.wasShortPressedRaw(HalGPIO::BTN_RIGHT, 600);

  // Side UP: short=next, long=next bookmark
  const bool sideUpShort   = mappedInput.wasShortPressedRaw(HalGPIO::BTN_UP, 500);
  const bool sideUpLong    = mappedInput.wasLongPressedRaw(HalGPIO::BTN_UP, 500);
  // Side DOWN: short=prev, long=toggle bookmark
  const bool sideDownShort = mappedInput.wasShortPressedRaw(HalGPIO::BTN_DOWN, 500);
  const bool sideDownLong  = mappedInput.wasLongPressedRaw(HalGPIO::BTN_DOWN, 500);

  // Power button short press = next page (when configured)
  const bool powerNextShort = (SETTINGS.shortPwrBtn == CrossPointSettings::PAGE_TURN) &&
                               mappedInput.wasShortPressedRaw(HalGPIO::BTN_POWER, SETTINGS.getPowerButtonDuration());

  // Combination keys for bookmarks:
  // LB (LEFT Cluster: Back/Confirm) + Side DOWN (RD) = Toggle Bookmark
  // LB (LEFT Cluster: Back/Confirm) + Side UP (RU)   = Next Bookmark
  // RB (RIGHT Cluster: Left/Right)  + Side UP/DOWN   = Jump +/- 10%
  const bool lbPressed = mappedInput.isPressedRaw(HalGPIO::BTN_BACK) || mappedInput.isPressedRaw(HalGPIO::BTN_CONFIRM);
  const bool rbPressed = mappedInput.isPressedRaw(HalGPIO::BTN_LEFT) || mappedInput.isPressedRaw(HalGPIO::BTN_RIGHT);

  if (lbPressed && mappedInput.wasPressedRaw(HalGPIO::BTN_DOWN)) {
    // LB + Side Down = Skip -10 pages
    currentPage = (currentPage >= 10) ? currentPage - 10 : 0;
    if (mappedInput.isPressedRaw(HalGPIO::BTN_BACK)) mappedInput.consumeButtonRaw(HalGPIO::BTN_BACK);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_CONFIRM)) mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_DOWN);
    requestUpdate();
    return;
  }
  if (lbPressed && mappedInput.wasPressedRaw(HalGPIO::BTN_UP)) {
    // LB + Side Up = Skip +10 pages
    currentPage += 10;
    if (currentPage >= xtc->getPageCount()) currentPage = xtc->getPageCount() - 1;
    if (mappedInput.isPressedRaw(HalGPIO::BTN_BACK)) mappedInput.consumeButtonRaw(HalGPIO::BTN_BACK);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_CONFIRM)) mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_UP);
    requestUpdate();
    return;
  }
  if (rbPressed && mappedInput.wasPressedRaw(HalGPIO::BTN_UP)) {
    jumpPercent(10);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_LEFT)) mappedInput.consumeButtonRaw(HalGPIO::BTN_LEFT);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_RIGHT)) mappedInput.consumeButtonRaw(HalGPIO::BTN_RIGHT);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_UP);
    return;
  }
  if (rbPressed && mappedInput.wasPressedRaw(HalGPIO::BTN_DOWN)) {
    jumpPercent(-10);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_LEFT)) mappedInput.consumeButtonRaw(HalGPIO::BTN_LEFT);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_RIGHT)) mappedInput.consumeButtonRaw(HalGPIO::BTN_RIGHT);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_DOWN);
    return;
  }

  int skipAmount = 0;
  if (sideUpLong) {
    nextBookmark();
    return;
  } else if (sideDownLong) {
    toggleBookmark();
    return;
  }
 else if (frontRightShort || sideUpShort || powerNextShort)  skipAmount = 1;
  else if (frontLeftShort  || sideDownShort) skipAmount = -1;

  if (skipAmount == 0) return;

  if (skipAmount < 0) {
    uint32_t positiveSkip = static_cast<uint32_t>(-skipAmount);
    if (currentPage >= positiveSkip) {
      currentPage -= positiveSkip;
    } else {
      currentPage = 0;
    }
  } else {
    currentPage += static_cast<uint32_t>(skipAmount);
    if (currentPage >= xtc->getPageCount()) {
      currentPage = xtc->getPageCount();  // Allow showing "End of book"
    }
  }
  requestUpdate();
}

void XtcReaderActivity::render(Activity::RenderLock&&) {
  if (inMenu) {
    renderMenu();
    return;
  }
  if (!xtc) {
    return;
  }

  // Bounds check
  if (currentPage >= xtc->getPageCount()) {
    // Show end of book screen
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  renderPage();
  saveProgress();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void XtcReaderActivity::renderMenu() const {
  // Composite page background WITHOUT triggering display or grayscale passes
  const_cast<XtcReaderActivity*>(this)->renderPage(false);

  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const bool darkMode = SETTINGS.darkMode;
  const bool textColor = !darkMode;

  if (inScrubber) {
    const int pw = 360;
    const int ph = 220;
    const int px = (sw - pw) / 2;
    const int py = (sh - ph) / 2;
    renderer.fillRoundedRect(px, py, pw, ph, 10, darkMode ? Color::Black : Color::White);
    renderer.drawRoundedRect(px, py, pw, ph, 2, 10, textColor);
    renderer.drawCenteredText(UI_12_FONT_ID, py + 35, "Go to", textColor, EpdFontFamily::BOLD);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", scrubberPercent);
    renderer.drawCenteredText(UI_12_FONT_ID, py + 78, pctBuf, textColor, EpdFontFamily::BOLD);
    const int barW = pw - 60;
    const int barH = 14;
    const int barX = px + 30;
    const int barY = py + 118;
    renderer.drawRoundedRect(barX, barY, barW, barH, 1, 3, textColor);
    const int fillW = (barW - 4) * scrubberPercent / 100;
    if (fillW > 0) renderer.fillRect(barX + 2, barY + 2, fillW, barH - 4, textColor);
    renderer.fillRect(barX + 2 + fillW - 2, barY - 4, 4, barH + 8, textColor);
    renderer.drawCenteredText(SMALL_FONT_ID, py + 165, "< > +-1%   UP/DN +-10%", textColor);
    renderer.drawCenteredText(SMALL_FONT_ID, py + 192, "Confirm: jump   Back: cancel", textColor);
    renderer.displayBuffer();
    return;
  }

  const int mw = 320;
  const int mh = 330;
  const int mx = (sw - mw) / 2;
  const int my = (sh - mh) / 2;

  // Border and Background
  renderer.fillRoundedRect(mx, my, mw, mh, 10, darkMode ? Color::Black : Color::White);
  renderer.drawRoundedRect(mx, my, mw, mh, 2, 10, textColor);

  const char* options[] = {"Resume", "Go to",
                           darkMode ? "Day Mode" : "Dark Mode", "Screenshot", "Exit"};

  for (int i = 0; i < 5; i++) {
    int ry = my + 45 + (i * 55);
    if (menuSelectedIndex == i) {
      renderer.fillRoundedRect(mx + 10, ry - 5, mw - 20, 40, 8, textColor ? Color::Black : Color::White);
    }
    renderer.drawText(UI_12_FONT_ID, mx + 20, ry + 2, options[i], (menuSelectedIndex != i) ? textColor : darkMode);
  }

  renderer.displayBuffer();
}

void XtcReaderActivity::renderPage(bool triggerDisplay) {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  // Calculate buffer size for one page
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  // Allocate or resize page buffer if needed
  if (!pageBuffer || pageBufferSize > pageBufferCapacity) {
    if (pageBuffer) {
      free(pageBuffer);
    }
    pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
    if (!pageBuffer) {
      LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
      pageBufferCapacity = 0;
      renderer.clearScreen();
      renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
      renderer.displayBuffer();
      return;
    }
    pageBufferCapacity = pageBufferSize;
    LOG_DBG("XTR", "Allocated page buffer: %lu bytes", pageBufferCapacity);
  }

  // Load page data
  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu", currentPage);
    // Note: we don't free the buffer here, we keep it for potential future successful loads
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Clear screen first - black in dark mode, white otherwise
  renderer.clearScreen(SETTINGS.darkMode ? 0x00 : 0xFF);

  // Copy page bitmap using GfxRenderer's drawPixel
  // XTC/XTCH pages are pre-rendered with status bar included, so render full page
  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    // XTH 2-bit mode: Two bit planes, column-major order
    // - Columns scanned right to left (x = width-1 down to 0)
    // - 8 vertical pixels per byte (MSB = topmost pixel in group)
    // - First plane: Bit1, Second plane: Bit2
    // - Pixel value = (bit1 << 1) | bit2
    // - Grayscale: 0=White, 1=Dark Grey, 2=Light Grey, 3=Black

    const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
    const uint8_t* plane1 = pageBuffer;              // Bit1 plane
    const uint8_t* plane2 = pageBuffer + planeSize;  // Bit2 plane
    const size_t colBytes = (pageHeight + 7) / 8;    // Bytes per column (100 for 800 height)

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    // Optimized grayscale rendering without storeBwBuffer (saves 48KB peak memory)
    // Flow: BW display → LSB/MSB passes → grayscale display → re-render BW for next frame


    // Pass 1: BW buffer - draw pixels (inverted in Dark Mode)
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        const uint8_t val = getPixelValue(x, y);
        if (SETTINGS.darkMode) {
          // Dark Mode: Background is Black. Draw original darker pixels (val>=1) as White (false).
          if (val >= 1) {
            renderer.drawPixel(x, y, false);
          }
        } else {
          // Normal Mode: Background is White. Draw non-White (val>=1) as Black (true).
          if (val >= 1) {
            renderer.drawPixel(x, y, true);
          }
        }
      }
    }

    // Display BW with conditional refresh based on pagesUntilFullRefresh
    if (triggerDisplay) {
      if (pagesUntilFullRefresh <= 1) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
      } else {
        renderer.displayBuffer();
        pagesUntilFullRefresh--;
      }
    }

    // Pass 2 & 3: Skip grayscale overlay in Dark Mode to ensure high contrast
    if (!SETTINGS.darkMode) {
      // (Keep existing Pass 2 & 3 logic for normal mode)
      renderer.clearScreen(0x00);
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          if (getPixelValue(x, y) == 1) {  // Dark grey only
            renderer.drawPixel(x, y, false);
          }
        }
      }
      renderer.copyGrayscaleLsbBuffers();

      renderer.clearScreen(0x00);
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          const uint8_t pv = getPixelValue(x, y);
          if (pv == 1 || pv == 2) {  // Dark grey or Light grey
            renderer.drawPixel(x, y, false);
          }
        }
      }
      renderer.copyGrayscaleMsbBuffers();
      if (triggerDisplay) {
        renderer.displayGrayBuffer();
      }

      // Pass 4: Re-render BW to framebuffer (RESTORE for next frame)
      // We ONLY do this in normal mode because in Dark Mode, Pass 1 already left the correct inverted image in the buffer.
      renderer.clearScreen(0xFF);
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          if (getPixelValue(x, y) >= 1) {
            renderer.drawPixel(x, y, true);
          }
        }
      }
      if (triggerDisplay) {
        renderer.cleanupGrayscaleWithFrameBuffer();
      }
    } else {
        // In Dark Mode, we don't need to re-render. Pass 1 already drew the inverted page.
        // But we MUST re-render the status bar so it appears on the PHYSICAL screen if it wasn't part of the BW pass.
    }

    // Overlay status bar on top of the rendered page bitmap
    renderBookmarkIndicator();
    renderStatusBar();
    
    // In Dark Mode, because we didn't call displayGrayBuffer, we might need a final displayBuffer 
    // to ensure the Status Bar (rendered AFTER Pass 1's displayBuffer) is actually shown.
    if (triggerDisplay && SETTINGS.darkMode) {
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }

    LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit grayscale)", currentPage + 1, xtc->getPageCount());
    return;
  } else {
    // 1-bit mode: 8 pixels per byte, MSB first
    const size_t srcRowBytes = (pageWidth + 7) / 8;  // 60 bytes for 480 width

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      const size_t srcRowStart = srcY * srcRowBytes;

      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        // Read source pixel (MSB first, bit 7 = leftmost pixel)
        const size_t srcByte = srcRowStart + srcX / 8;
        const size_t srcBit = 7 - (srcX % 8);
        const bool isBlackRaw = !((pageBuffer[srcByte] >> srcBit) & 1);  // XTC: 0 = black, 1 = white

        if (SETTINGS.darkMode) {
          // Background is Black. Draw original Black (isBlackRaw=true) as White (false).
          if (isBlackRaw) {
            renderer.drawPixel(srcX, srcY, false);
          }
        } else {
          // Background is White. Draw original Black (isBlackRaw=true) as Black (true).
          if (isBlackRaw) {
            renderer.drawPixel(srcX, srcY, true);
          }
        }
      }
    }
  }
  // White pixels are already cleared by clearScreen()

  // Overlay status bar on top of the rendered page bitmap
  renderBookmarkIndicator();
  renderStatusBar();

  // Display with appropriate refresh
  if (triggerDisplay) {
    if (pagesUntilFullRefresh <= 1) {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayBuffer();
      pagesUntilFullRefresh--;
    }
  }

  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);
}

void XtcReaderActivity::renderStatusBar() const {
  if (SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NONE) {
    return;
  }

  const int screenHeight = renderer.getScreenHeight();
  const int screenWidth = renderer.getScreenWidth();
  constexpr int marginBottom = 4;  // pixels from bottom of screen to text baseline
  const int textY = screenHeight - marginBottom - renderer.getLineHeight(SMALL_FONT_ID);

  const size_t totalPages = xtc->getPageCount();
  const bool textColor = !SETTINGS.darkMode;

  const int y = screenHeight - 15;
  const int sideMargin = 24;
  const int availableWidth = screenWidth - (sideMargin * 2);
  
  // 1px track (thin)
  renderer.fillRect(sideMargin, y + 2, availableWidth, 1, textColor);
  
  // 3px progress (thick)
  if (totalPages > 0) {
    int progressWidth = (static_cast<long>(currentPage + 1) * availableWidth) / totalPages;
    if (progressWidth > availableWidth) progressWidth = availableWidth;
    renderer.fillRect(sideMargin, y, progressWidth, 3, textColor);

    // Draw Bookmark Markers (2x2px)
    for (uint32_t b : bookmarks) {
      int markerX = sideMargin + (static_cast<long>(b + 1) * availableWidth) / totalPages;
      if (markerX >= sideMargin && markerX < sideMargin + availableWidth) {
        // Draw 2x2 square overlapping with the track for a joined look
        renderer.fillRect(markerX - 1, y, 2, 2, textColor);
      }
    }

    // Draw page number (small text, right aligned above track)
    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "%lu", (unsigned long)(currentPage + 1));
    int textWidth = renderer.getTextWidth(SMALL_FONT_ID, pageBuf);
    renderer.drawText(SMALL_FONT_ID, sideMargin + availableWidth - textWidth, y - renderer.getLineHeight(SMALL_FONT_ID), pageBuf, 
                      SETTINGS.darkMode ? Color::LightGray : Color::DarkGray);
  }
}

void XtcReaderActivity::renderBookmarkIndicator() const {
  if (isPageBookmarked(currentPage)) {
    const int sw = renderer.getScreenWidth();
    const int rw = 20;
    const int rh = 40;
    const int rx = sw - rw - 30;
    const int ry = 0;
    
    // Draw ribbon (inverted when in dark mode to stay visible)
    renderer.fillRect(rx, ry, rw, rh, !SETTINGS.darkMode);
  }
}

void XtcReaderActivity::saveProgress() const {
  FsFile f;
  if (Storage.openFileForWrite("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = (currentPage >> 16) & 0xFF;
    data[3] = (currentPage >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void XtcReaderActivity::loadProgress() {
  FsFile f;
  if (Storage.openFileForRead("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      LOG_DBG("XTR", "Loaded progress: page %lu", currentPage);

      // Validate page number
      if (currentPage >= xtc->getPageCount()) {
        currentPage = 0;
      }
    }
    f.close();
  }
}

void XtcReaderActivity::jumpToPercent(int percent) {
  if (!xtc) return;
  const size_t total = xtc->getPageCount();
  if (total == 0) return;
  int target = (static_cast<int>(total) * percent) / 100;
  if (target < 0) target = 0;
  if (target >= (int)total) target = (int)total - 1;
  currentPage = static_cast<uint32_t>(target);
  requestUpdate();
}

void XtcReaderActivity::jumpPercent(int deltaPercent) {
  if (!xtc) return;
  size_t total = xtc->getPageCount();
  if (total == 0) return;

  int deltaPages = (static_cast<int>(total) * deltaPercent) / 100;
  if (deltaPages == 0) deltaPages = (deltaPercent > 0) ? 1 : -1;

  int targetPage = static_cast<int>(currentPage) + deltaPages;
  if (targetPage < 0) targetPage = 0;
  if (targetPage >= (int)total) targetPage = (int)total - 1;

  currentPage = static_cast<uint32_t>(targetPage);
  requestUpdate();
}

void XtcReaderActivity::saveBookmarks() const {
  FsFile f;
  if (Storage.openFileForWrite("XTR", xtc->getCachePath() + "/bookmarks.bin", f)) {
    for (uint32_t b : bookmarks) {
      uint8_t data[4];
      data[0] = b & 0xFF;
      data[1] = (b >> 8) & 0xFF;
      data[2] = (b >> 16) & 0xFF;
      data[3] = (b >> 24) & 0xFF;
      f.write(data, 4);
    }
    f.close();
  }
}

void XtcReaderActivity::loadBookmarks() {
  bookmarks.clear();
  FsFile f;
  if (Storage.openFileForRead("XTR", xtc->getCachePath() + "/bookmarks.bin", f)) {
    uint8_t data[4];
    while (f.read(data, 4) == 4) {
      uint32_t b = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      bookmarks.push_back(b);
    }
    f.close();
    std::sort(bookmarks.begin(), bookmarks.end());
  }
}

void XtcReaderActivity::toggleBookmark() {
  auto it = std::find(bookmarks.begin(), bookmarks.end(), currentPage);
  if (it != bookmarks.end()) {
    bookmarks.erase(it);
  } else {
    bookmarks.push_back(currentPage);
    std::sort(bookmarks.begin(), bookmarks.end());
  }
  saveBookmarks();
  requestUpdate();
}

void XtcReaderActivity::nextBookmark() {
  if (bookmarks.empty()) return;

  auto it = std::upper_bound(bookmarks.begin(), bookmarks.end(), currentPage);
  if (it == bookmarks.end()) {
    currentPage = bookmarks[0];
  } else {
    currentPage = *it;
  }
  requestUpdate();
}

bool XtcReaderActivity::isPageBookmarked(uint32_t page) const {
  return std::find(bookmarks.begin(), bookmarks.end(), page) != bookmarks.end();
}
