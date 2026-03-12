#include "TxtReaderActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr int statusBarMargin = 25;
constexpr int progressBarMarginTop = 1;
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading

// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 2;          // Increment when cache format changes
}  // namespace

void TxtReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);  // Clear ghosting before reader content

  if (!txt) {
    return;
  }

  // Configure screen orientation based on settings
  switch (SETTINGS.orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }

  txt->setupCacheDir();

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");
  READING_STATS.recordOpen(filePath, fileName);

  sessionStartMillis = millis();

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (sessionStartMillis > 0 && txt) {
    auto filePath = txt->getPath();
    auto fileName = filePath.substr(filePath.rfind('/') + 1);
    uint32_t elapsedSeconds = (millis() - sessionStartMillis) / 1000;
    READING_STATS.addReadingTime(filePath, fileName, elapsedSeconds);
    READING_STATS.saveToFile();
    sessionStartMillis = 0;
  }

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  pageOffsets.clear();
  currentPageLines.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  txt.reset();
}

void TxtReaderActivity::loop() {
  ActivityWithSubactivity::loop();
  if (subActivity) {
    return;
  }

  if (skipNextButtonCheck) {
    if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // === Custom Fixed Button Layout ===
  // Front LEFT cluster (BACK+CONFIRM): short=prev, long=home
  // Front RIGHT cluster (LEFT+RIGHT): short=next, long=menu
  // Side UP: short=next page, long=+10 pages
  // Side DOWN: short=prev page, long=-10 pages
  const unsigned long longPressMs = 350;

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

    const int optionCount = 6; // Resume, Go to, Dark Mode, Orientation, Screenshot, Exit
    if (mappedInput.wasReleasedRaw(HalGPIO::BTN_UP)) {
      menuSelectedIndex = (menuSelectedIndex > 0) ? menuSelectedIndex - 1 : optionCount - 1;
      requestUpdate();
    } else if (mappedInput.wasReleasedRaw(HalGPIO::BTN_DOWN)) {
      menuSelectedIndex = (menuSelectedIndex < optionCount - 1) ? menuSelectedIndex + 1 : 0;
      requestUpdate();
    } else if (mappedInput.wasReleasedAnyOf(HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM)) {
      // Cancel menu
      inMenu = false;
      requestUpdate();
    } else if (mappedInput.wasReleasedAnyOf(HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT)) {
      // Confirm selection
      switch (menuSelectedIndex) {
        case 0: // Resume
          inMenu = false;
          break;
        case 1: { // Go to — inline scrubber
          scrubberPercent = (totalPages > 1) ? (currentPage * 100 / (totalPages - 1)) : 0;
          inScrubber = true;
          requestUpdate();
          return;
        }
        case 2: // Dark Mode
          inMenu = false;
          SETTINGS.darkMode = !SETTINGS.darkMode;
          SETTINGS.saveToFile();
          break;
        case 3: { // Orientation
          inMenu = false;
          uint8_t nextOrientation = (SETTINGS.orientation + 1) % 4;
          SETTINGS.orientation = nextOrientation;
          SETTINGS.saveToFile();
          switch (nextOrientation) {
            case CrossPointSettings::ORIENTATION::PORTRAIT:
              renderer.setOrientation(GfxRenderer::Orientation::Portrait); break;
            case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
              renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise); break;
            case CrossPointSettings::ORIENTATION::INVERTED:
              renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted); break;
            case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
              renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise); break;
            default: break;
          }
          initialized = false;
          break;
        }
        case 4: // Screenshot
          inMenu = false;
          ScreenshotUtil::takeScreenshot(renderer);
          break;
        case 5: // Exit
          inMenu = false;
          onGoHome();
          return;
      }
      requestUpdate();
    }
    return;
  }

  // Front LEFT long press -> home
  if (mappedInput.isPressedAnyOf(HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM) &&
      mappedInput.getHeldTime() >= 800) {
    onGoHome();
    return;
  }

  // Front RIGHT long press -> menu
  if (mappedInput.wasReleasedAnyOf(HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT) &&
      mappedInput.getHeldTime() >= longPressMs) {
    renderer.storeBwBuffer();
    inMenu = true;
    menuSelectedIndex = 0;
    requestUpdate();
    return;
  }

  const bool frontLeftShort = mappedInput.wasReleasedAnyOf(HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM) &&
                              mappedInput.getHeldTime() < 800;
  const bool frontRightShort = mappedInput.wasReleasedAnyOf(HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT) &&
                               mappedInput.getHeldTime() < longPressMs;

  // Side UP: short=next, long=+10
  const bool sideUpShort   = mappedInput.wasReleasedRaw(HalGPIO::BTN_UP)  && mappedInput.getHeldTime() < longPressMs;
  const bool sideUpLong    = mappedInput.wasLongPressed(MappedInputManager::Button::Up, longPressMs);
  // Side DOWN: short=prev, long=-10
  const bool sideDownShort = mappedInput.wasReleasedRaw(HalGPIO::BTN_DOWN) && mappedInput.getHeldTime() < longPressMs;
  const bool sideDownLong  = mappedInput.wasLongPressed(MappedInputManager::Button::Down, longPressMs);

  // Power button short press = next page (when configured)
  const bool powerNextShort = (SETTINGS.shortPwrBtn == CrossPointSettings::PAGE_TURN) &&
                               mappedInput.wasShortPressedRaw(HalGPIO::BTN_POWER, SETTINGS.getPowerButtonDuration());

  const bool lbPressed = mappedInput.isPressedAnyOf(HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM);

  if (lbPressed && mappedInput.wasReleasedRaw(HalGPIO::BTN_UP)) {
    // LB + Side Up = Jump +10
    int target = static_cast<int>(currentPage) + 10;
    if (target >= totalPages) target = totalPages - 1;
    currentPage = static_cast<uint32_t>(target);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_UP);
    requestUpdate();
    return;
  }
  if (lbPressed && mappedInput.wasReleasedRaw(HalGPIO::BTN_DOWN)) {
    // LB + Side Down = Jump -10
    int target = static_cast<int>(currentPage) - 10;
    if (target < 0) target = 0;
    currentPage = static_cast<uint32_t>(target);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_DOWN);
    requestUpdate();
    return;
  }

  int delta = 0;
  if (sideUpLong)           delta = 10;
  else if (sideDownLong)    delta = -10;
  else if (frontRightShort || sideUpShort || powerNextShort)  delta = 1;
  else if (frontLeftShort  || sideDownShort) delta = -1;

  int targetPage = static_cast<int>(currentPage) + delta;
  if (targetPage < 0) targetPage = 0;
  if (targetPage >= totalPages) targetPage = totalPages - 1;

  if (currentPage != static_cast<uint32_t>(targetPage)) {
    currentPage = static_cast<uint32_t>(targetPage);
    requestUpdate();
  }
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += cachedScreenMargin;
  orientedMarginLeft += cachedScreenMargin;
  orientedMarginRight += cachedScreenMargin;
  orientedMarginBottom += cachedScreenMargin;

  const auto& metrics = UITheme::getInstance().getMetrics();

  // Add status bar margin
  if (SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::SIMPLE) {
    orientedMarginBottom += statusBarMargin - cachedScreenMargin;
  }

  viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex();
    // Save to cache for next time
    savePageIndexCache();
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

void TxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    // Word wrap if needed
    while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
      int lineWidth = renderer.getTextWidth(cachedFontId, line.c_str());

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;  // Consumed entire display content
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextWidth(cachedFontId, line.substr(0, breakPos).c_str()) > viewportWidth) {
        // Try to break at space
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          // Make sure we don't break in the middle of a UTF-8 sequence
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      // Skip space at break point
      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    }

    // Determine how much of the source buffer we consumed
    if (line.empty()) {
      // Fully consumed this source line, move past the newline
      pos = lineEnd + 1;
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      pos = pos + lineBytePos;
      break;
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);

  return !outLines.empty();
}

void TxtReaderActivity::render(Activity::RenderLock&&) {
  if (inMenu) {
    renderMenu();
    return;
  }

  if (!txt) {
    return;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();
  renderer.clearFontCache();

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += cachedScreenMargin;
  orientedMarginLeft += cachedScreenMargin;
  orientedMarginRight += cachedScreenMargin;
  orientedMarginBottom += statusBarMargin;

  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = orientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = orientedMarginLeft;

        // Apply text alignment
        switch (cachedParagraphAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set to left margin
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = orientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = orientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            // For plain text, justified is treated as left-aligned
            // (true justification would require word spacing adjustments)
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineHeight;
    }
  };

  // First pass: BW rendering
  renderLines();
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Grayscale rendering pass (for anti-aliased fonts)
  if (SETTINGS.textAntiAliasing) {
    // Save BW buffer for restoration after grayscale pass
    renderer.storeBwBuffer();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderLines();
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderLines();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    // Restore BW buffer
    renderer.restoreBwBuffer();
  }
}

void TxtReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) const {
  if (SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NONE) {
    return;
  }

  const auto screenHeight = renderer.getScreenHeight();
  const auto textY = screenHeight - orientedMarginBottom - 4;

  char progressStr[32];
  snprintf(progressStr, sizeof(progressStr), "%d / %d", currentPage + 1, totalPages);

  int progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
  int xPos = orientedMarginLeft + (viewportWidth - progressTextWidth) / 2;

  renderer.drawText(SMALL_FONT_ID, xPos, textY, progressStr);
}

void TxtReaderActivity::saveProgress() const {
  FsFile f;
  if (Storage.openFileForWrite("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    f.write(data, 4);
    f.close();
  }
}

void TxtReaderActivity::loadProgress() {
  FsFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
    f.close();
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  // Cache file format (using serialization module):
  // - uint32_t: magic "TXTI"
  // - uint8_t: cache version
  // - uint32_t: file size (to validate cache)
  // - int32_t: viewport width
  // - int32_t: lines per page
  // - int32_t: font ID (to invalidate cache on font change)
  // - int32_t: screen margin (to invalidate cache on margin change)
  // - uint8_t: paragraph alignment (to invalidate cache on alignment change)
  // - uint32_t: total pages count
  // - N * uint32_t: page offsets

  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  // Read and validate header using serialization module
  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    f.close();
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    f.close();
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    f.close();
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    f.close();
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    f.close();
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    f.close();
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    f.close();
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    f.close();
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  // Read page offsets
  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  f.close();
  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::jumpToPercent(int percent) {
  if (totalPages <= 0) return;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  int targetPage = (percent * (totalPages - 1)) / 100;
  if (targetPage < 0) targetPage = 0;
  if (targetPage >= totalPages) targetPage = totalPages - 1;
  currentPage = static_cast<uint32_t>(targetPage);
}

void TxtReaderActivity::renderMenu() const {
  if (!renderer.storeBwBuffer()) {
    renderer.clearScreen();
  }
  renderer.restoreBwBuffer();
  renderer.storeBwBuffer();

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

  renderer.fillRoundedRect(mx, my, mw, mh, 10, darkMode ? Color::Black : Color::White);
  renderer.drawRoundedRect(mx, my, mw, mh, 2, 10, textColor);

  const char* options[] = {"Resume", "Go to",
                           darkMode ? "Day Mode" : "Dark Mode",
                           "Orientation", "Screenshot", "Exit"};

  for (int i = 0; i < 6; i++) {
    int ry = my + 15 + (i * 50);
    if (menuSelectedIndex == i) {
      renderer.fillRoundedRect(mx + 10, ry - 5, mw - 20, 40, 8, textColor ? Color::Black : Color::White);
    }
    renderer.drawText(UI_12_FONT_ID, mx + 20, ry + 2, options[i], (menuSelectedIndex != i) ? textColor : darkMode);
  }

  renderer.displayBuffer();
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  // Write header using serialization module
  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  // Write page offsets
  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  f.close();
  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}
