#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "ReadingStatsStore.h"
#include "util/ScreenshotUtil.h"
#include <algorithm>

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr int statusBarMargin = 19;
constexpr int progressBarMarginTop = 1;

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

// Apply the logical reader orientation to the renderer.
// This centralizes orientation mapping so we don't duplicate switch logic elsewhere.
void applyReaderOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
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
}

}  // namespace

void EpubReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderer.setDarkMode(false);  // Reader manages its own dark mode inversion
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);  // Clear ghosting before reader content

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  applyReaderOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();

  FsFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
    f.close();
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath(), epub->getFileSize());
  READING_STATS.recordOpen(epub->getPath(), epub->getTitle());

  sessionStartMillis = millis();

  // Trigger first update
  loadBookmarks();
  skipNextButtonCheck = true;
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (sessionStartMillis > 0 && epub) {
    uint32_t elapsedSeconds = (millis() - sessionStartMillis) / 1000;
    READING_STATS.addReadingTime(epub->getPath(), epub->getTitle(), elapsedSeconds);
    READING_STATS.saveToFile();
    sessionStartMillis = 0;
  }

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.setDarkMode(SETTINGS.darkMode);  // Restore renderer dark mode for UI

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  section.reset();
  epub.reset();
}

void EpubReaderActivity::loop() {
  // Handle pending subactivities from base class
  ActivityWithSubactivity::loop();

  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    // subActivity->loop() is already called by the base class loop() above.
    // Deferred exit: process after subActivity->loop() returns to avoid use-after-free
    if (pendingSubactivityExit) {
      pendingSubactivityExit = false;
      exitActivity();
      requestUpdate();
      skipNextButtonCheck = true;  // Skip button processing to ignore stale events
    }
    // Deferred go home: process after subActivity->loop() returns to avoid race condition
    if (pendingGoHome) {
      pendingGoHome = false;
      exitActivity();
      if (onGoHome) {
        onGoHome();
      }
      return;  // Don't access 'this' after callback
    }
    return;
  }

  // Handle pending go home when no subactivity (e.g., from long press back)
  if (pendingGoHome) {
    pendingGoHome = false;
    if (onGoHome) {
      onGoHome();
    }
    return;  // Don't access 'this' after callback
  }

  // Skip button processing after returning from subactivity
  // This prevents stale button release events from triggering actions
  // We wait until: (1) all relevant buttons are released, AND (2) wasReleased events have been cleared
  if (skipNextButtonCheck) {
    const bool confirmCleared = !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                                !mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    const bool backCleared = !mappedInput.isPressed(MappedInputManager::Button::Back) &&
                             !mappedInput.wasReleased(MappedInputManager::Button::Back);
    if (confirmCleared && backCleared) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // === Custom Fixed Button Layout ===
  // Front LEFT cluster (BACK + CONFIRM): short=prev page, long=home/file select
  // Front RIGHT cluster (LEFT + RIGHT): short=next page, long=reader menu
  // Side UP:   short=next page, long=+10 pages
  // Side DOWN: short=prev page, long=-10 pages
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

    const int optionCount = 7; // Resume, TOC, Go to, Dark Mode, Orientation, Screenshot, Exit
    if (mappedInput.wasReleasedRaw(HalGPIO::BTN_UP)) {
      menuSelectedIndex = (menuSelectedIndex > 0) ? menuSelectedIndex - 1 : optionCount - 1;
      requestUpdate();
    } else if (mappedInput.wasReleasedRaw(HalGPIO::BTN_DOWN)) {
      menuSelectedIndex = (menuSelectedIndex < optionCount - 1) ? menuSelectedIndex + 1 : 0;
      requestUpdate();
    } else if (mappedInput.wasShortPressed(MappedInputManager::Button::Confirm)) {
      // Execute Menu Action (Right Cluster)
      if (menuSelectedIndex == 2) { // Go to — inline scrubber
        float bookProgress = 0.0f;
        if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
          const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
          bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
        }
        scrubberPercent = static_cast<int>(bookProgress + 0.5f);
        if (scrubberPercent < 0) scrubberPercent = 0;
        if (scrubberPercent > 100) scrubberPercent = 100;
        inScrubber = true;
        requestUpdate();
        return;
      }
      inMenu = false;
      skipNextButtonCheck = true;
      switch (menuSelectedIndex) {
        case 0: break; // Resume
        case 1: onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER); return;
        case 3:
          SETTINGS.darkMode = !SETTINGS.darkMode;
          SETTINGS.saveToFile();
          break;
        case 4: onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN); break;
        case 5: onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SCREENSHOT); break;
        case 6:
          mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
          onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::GO_HOME);
          return;
      }
      requestUpdate();
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

  // Front LEFT cluster: short=prev page, long=go home (snappy)
  if (mappedInput.wasLongPressedRaw(HalGPIO::BTN_BACK, 800) || 
      mappedInput.wasLongPressedRaw(HalGPIO::BTN_CONFIRM, 800)) {
    mappedInput.consumeButtonRaw(HalGPIO::BTN_BACK);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
    onGoHome();
    return;
  }
  const bool frontLeftShort = mappedInput.wasShortPressedRaw(HalGPIO::BTN_BACK, 800) || 
                              mappedInput.wasShortPressedRaw(HalGPIO::BTN_CONFIRM, 800);

  // Front RIGHT cluster: short=next page, long=menu (snappy)
  if (mappedInput.wasLongPressedRaw(HalGPIO::BTN_LEFT, 600) ||
      mappedInput.wasLongPressedRaw(HalGPIO::BTN_RIGHT, 600)) {
    renderer.storeBwBuffer();
    inMenu = true;
    menuSelectedIndex = 0;
    requestUpdate();
    return;
  }
  const bool frontRightShort = mappedInput.wasShortPressedRaw(HalGPIO::BTN_LEFT, 600) ||
                               mappedInput.wasShortPressedRaw(HalGPIO::BTN_RIGHT, 600);

  // Side UP: short = next page, long = +10 pages
  const bool sideUpShort = mappedInput.wasShortPressedRaw(HalGPIO::BTN_UP, 500);
  const bool sideUpLong  = mappedInput.wasLongPressedRaw(HalGPIO::BTN_UP, 500);

  // Side DOWN: short = prev page, long = -10 pages
  const bool sideDownShort = mappedInput.wasShortPressedRaw(HalGPIO::BTN_DOWN, 500);
  const bool sideDownLong  = mappedInput.wasLongPressedRaw(HalGPIO::BTN_DOWN, 500);

  // Power button short press = next page (when configured)
  const bool powerNextShort = (SETTINGS.shortPwrBtn == CrossPointSettings::PAGE_TURN) &&
                               mappedInput.wasShortPressedRaw(HalGPIO::BTN_POWER, SETTINGS.getPowerButtonDuration());

  // Combination keys for bookmarks and navigation:
  // LB (LEFT Cluster: Back/Confirm) + Side DOWN (RD) = Toggle Bookmark
  // LB (LEFT Cluster: Back/Confirm) + Side UP (RU)   = Next Bookmark
  // RB (RIGHT Cluster: Left/Right)  + Side UP/DOWN   = Jump +/- 10%
  const bool lbPressed = mappedInput.isPressedRaw(HalGPIO::BTN_BACK) || mappedInput.isPressedRaw(HalGPIO::BTN_CONFIRM);
  const bool rbPressed = mappedInput.isPressedRaw(HalGPIO::BTN_LEFT) || mappedInput.isPressedRaw(HalGPIO::BTN_RIGHT);

  if (lbPressed && mappedInput.wasPressedRaw(HalGPIO::BTN_DOWN)) {
    // LB + Side Down = Skip -10 pages
    if (mappedInput.isPressedRaw(HalGPIO::BTN_BACK)) mappedInput.consumeButtonRaw(HalGPIO::BTN_BACK);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_CONFIRM)) mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_DOWN);
    
    // Trigger update with delta
    int delta = -10;
    if (section) {
      int targetPage = section->currentPage + delta;
      if (targetPage < 0 && currentSpineIndex > 0) {
        RenderLock lock(*this);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
        section.reset();
      } else if (targetPage < 0) {
        section->currentPage = 0;
      } else {
        section->currentPage = targetPage;
      }
    }
    requestUpdate();
    return;
  }
  if (lbPressed && mappedInput.wasPressedRaw(HalGPIO::BTN_UP)) {
    // LB + Side Up = Skip +10 pages
    if (mappedInput.isPressedRaw(HalGPIO::BTN_BACK)) mappedInput.consumeButtonRaw(HalGPIO::BTN_BACK);
    if (mappedInput.isPressedRaw(HalGPIO::BTN_CONFIRM)) mappedInput.consumeButtonRaw(HalGPIO::BTN_CONFIRM);
    mappedInput.consumeButtonRaw(HalGPIO::BTN_UP);

    int delta = 10;
    if (section) {
      int targetPage = section->currentPage + delta;
      if (targetPage >= section->pageCount && currentSpineIndex < (epub ? epub->getSpineItemsCount() - 1 : 0)) {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      } else if (targetPage >= section->pageCount) {
        section->currentPage = section->pageCount - 1;
      } else {
        section->currentPage = targetPage;
      }
    }
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

  if (!frontLeftShort && !frontRightShort && !sideUpShort && !sideUpLong && !sideDownShort && !sideDownLong && !powerNextShort) {
    return;
  }

  // Determine page delta
  int delta = 0;
  if (sideUpLong) {
    nextBookmark();
    return;
  } else if (sideDownLong) {
    toggleBookmark();
    return;
  } else if (frontRightShort || sideUpShort || powerNextShort) delta = 1;
  else if (frontLeftShort || sideDownShort) delta = -1;

  if (delta == 0) {
    requestUpdate();
    return;
  }

  if (!section) {
    requestUpdate();
    return;
  }

  int targetPage = section->currentPage + delta;
  if (targetPage < 0) {
    if (currentSpineIndex > 0) {
      RenderLock lock(*this);
      nextPageNumber = UINT16_MAX;
      currentSpineIndex--;
      section.reset();
    } else {
      section->currentPage = 0;
    }
  } else if (targetPage >= section->pageCount) {
    if (currentSpineIndex < epub->getSpineItemsCount() - 1) {
      RenderLock lock(*this);
      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
    } else {
      section->currentPage = section->pageCount - 1;
    }
  } else {
    section->currentPage = targetPage;
  }

  requestUpdate();
}


void EpubReaderActivity::onReaderMenuBack(const uint8_t orientation) {
  exitActivity();
  // Apply the user-selected orientation when the menu is dismissed.
  // This ensures the menu can be navigated without immediately rotating the screen.
  applyOrientation(orientation);
  requestUpdate();
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      // Calculate values BEFORE we start destroying things
      const int currentP = section ? section->currentPage : 0;
      const int totalP = section ? section->pageCount : 0;
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();

      // 1. Close the menu
      exitActivity();

      // 2. Open the Chapter Selector
      enterNewActivity(new EpubReaderChapterSelectionActivity(
          this->renderer, this->mappedInput, epub, path, spineIdx, currentP, totalP,
          [this] {
            exitActivity();
            requestUpdate();
          },
          [this](const int newSpineIndex) {
            if (currentSpineIndex != newSpineIndex) {
              currentSpineIndex = newSpineIndex;
              nextPageNumber = 0;
              section.reset();
            }
            exitActivity();
            requestUpdate();
          },
          [this](const int newSpineIndex, const int newPage) {
            if (currentSpineIndex != newSpineIndex || (section && section->currentPage != newPage)) {
              currentSpineIndex = newSpineIndex;
              nextPageNumber = newPage;
              section.reset();
            }
            exitActivity();
            requestUpdate();
          }));

      break;
    }
    case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN: {
      uint8_t nextOrientation = (SETTINGS.orientation + 1) % 4;
      applyOrientation(nextOrientation);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      // Launch the slider-based percent selector and return here on confirm/cancel.
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      exitActivity();
      enterNewActivity(new EpubReaderPercentSelectionActivity(
          renderer, mappedInput, initialPercent,
          [this](const int percent) {
            // Apply the new position and exit back to the reader.
            jumpToPercent(percent);
            exitActivity();
            requestUpdate();
          },
          [this]() {
            // Cancel selection and return to the reader.
            exitActivity();
            requestUpdate();
          }));
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      // Defer go home to avoid race condition with display task
      pendingGoHome = true;
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub) {
          // 2. BACKUP: Read current progress
          // We use the current variables that track our position
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;

          section.reset();
          // 3. WIPE: Clear the cache directory
          epub->clearCache();

          // 4. RESTORE: Re-setup the directory and rewrite the progress file
          epub->setupCacheDir();

          saveProgress(backupSpine, backupPage, backupPageCount);
        }
      }
      // Defer go home to avoid race condition with display task
      pendingGoHome = true;
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      exitActivity();
      requestUpdate();
      break;
    }
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    applyReaderOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
}

// TODO: Failure handling
void EpubReaderActivity::render(Activity::RenderLock&& lock) {
  if (inMenu) {
    renderMenu();
    return;
  }

  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin + 30;
  orientedMarginLeft += SETTINGS.screenMargin + 16;
  orientedMarginRight += SETTINGS.screenMargin + 16;
  orientedMarginBottom += SETTINGS.screenMargin + 40;

  const auto& metrics = UITheme::getInstance().getMetrics();

  // Add status bar margin
  if (SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::SIMPLE) {
    orientedMarginBottom += statusBarMargin - SETTINGS.screenMargin;
  }

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle)) {
      LOG_DBG("ERS", "Cache not found, building...");

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                      viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, popupFn)) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
        section.reset();
        return;
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      section->currentPage = nextPageNumber;
    }

    // handles changes in reader settings and reset to approximate position based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount = 0;  // resets to 0 to prevent reading cached progress again
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      section->clearCache();
      section.reset();
      requestUpdate();  // Try again after clearing cache
      // TODO: prevent infinite loop if the page keeps failing to load for some reason
      return;
    }
    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    renderer.clearFontCache();
  }
  saveProgress(currentSpineIndex, section->currentPage, section->pageCount);

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}
void EpubReaderActivity::renderMenu() const {
  if (!renderer.storeBwBuffer()) {
    renderer.clearScreen();
  }
  renderer.restoreBwBuffer();
  // We MUST store it again immediately because restoreBwBuffer() frees the chunks!
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
    renderer.drawCenteredText(UI_12_FONT_ID, py + 35, tr(STR_GO_TO), textColor, EpdFontFamily::BOLD);
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
    renderer.drawCenteredText(SMALL_FONT_ID, py + 165, tr(STR_SCRUBBER_HINT_1), textColor);
    renderer.drawCenteredText(SMALL_FONT_ID, py + 192, tr(STR_SCRUBBER_HINT_2), textColor);
    renderer.displayBuffer();
    return;
  }

  const int mw = 320;
  const int mh = 380;
  const int mx = (sw - mw) / 2;
  const int my = (sh - mh) / 2;

  // Border and Background
  renderer.fillRoundedRect(mx, my, mw, mh, 10, darkMode ? Color::Black : Color::White);
  renderer.drawRoundedRect(mx, my, mw, mh, 2, 10, textColor);

  const char* options[] = {tr(STR_RESUME), tr(STR_TOC), tr(STR_GO_TO),
                           darkMode ? tr(STR_DAY_MODE) : tr(STR_DARK_MODE), tr(STR_ORIENTATION), tr(STR_SCREENSHOT_BUTTON), tr(STR_EXIT)};

  for (int i = 0; i < 7; i++) {
    int ry = my + 15 + (i * 50);
    if (menuSelectedIndex == i) {
      renderer.fillRoundedRect(mx + 10, ry - 5, mw - 20, 40, 8, textColor ? Color::Black : Color::White);
    }
    renderer.drawText(UI_12_FONT_ID, mx + 20, ry + 2, options[i], (menuSelectedIndex != i) ? textColor : darkMode);
  }

  renderer.displayBuffer();
}

void EpubReaderActivity::jumpPercent(int deltaPercent) {
  float bookProgress = 0.0f;
  if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
    const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
  }
  
  int targetPercent = clampPercent(static_cast<int>(bookProgress + 0.5f) + deltaPercent);
  jumpToPercent(targetPercent);
}


void EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  FsFile f;
  if (Storage.openFileForWrite("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[6];
    data[0] = currentSpineIndex & 0xFF;
    data[1] = (currentSpineIndex >> 8) & 0xFF;
    data[2] = currentPage & 0xFF;
    data[3] = (currentPage >> 8) & 0xFF;
    data[4] = pageCount & 0xFF;
    data[5] = (pageCount >> 8) & 0xFF;
    f.write(data, 6);
    f.close();
    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d", spineIndex, currentPage);
  } else {
    LOG_ERR("ERS", "Could not save progress!");
  }
}
void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  // Force special handling for pages with images when anti-aliasing is on
  bool imagePageWithAA = page->hasImages() && SETTINGS.textAntiAliasing;

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
  if (SETTINGS.darkMode) {
    renderer.invertScreen();
  }
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  if (imagePageWithAA) {
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
      renderBookmarkIndicator();
      renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderBookmarkIndicator();
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  } else if (pagesUntilFullRefresh <= 1) {
    renderBookmarkIndicator();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderBookmarkIndicator();
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Save bw buffer to reset buffer state after grayscale data sync
  renderer.storeBwBuffer();

  // grayscale rendering
  // TODO: Only do this if font supports it
  if (SETTINGS.textAntiAliasing) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleLsbBuffers();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleMsbBuffers();

    // display grayscale part
    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  // restore the bw data
  renderer.restoreBwBuffer();
}

void EpubReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                         const int orientedMarginLeft) const {
  if (SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NONE) {
    return;
  }

  const int screenHeight = renderer.getScreenHeight();
  const int viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const bool textColor = !SETTINGS.darkMode;
  const int y = screenHeight - 15;

  // Track (1px)
  renderer.fillRect(orientedMarginLeft, y + 2, viewportWidth, 1, textColor);

  // Book Progress (3px)
  float bookProgress = 0.0f;
  if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
    const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress);
    
    int progressWidth = static_cast<int>(bookProgress * viewportWidth);
    if (progressWidth > viewportWidth) progressWidth = viewportWidth;
    renderer.fillRect(orientedMarginLeft, y, progressWidth, 3, textColor);

    // Draw Bookmark Markers (2x2px)
    for (const auto& b : bookmarks) {
      int markerX = orientedMarginLeft + static_cast<int>(b.progress * viewportWidth);
      if (markerX >= orientedMarginLeft && markerX < orientedMarginLeft + viewportWidth) {
        // Draw 2x2 square overlapping with the track for a joined look
        renderer.fillRect(markerX - 1, y, 2, 2, textColor);
      }
    }

    // Draw page number (small text, right aligned within viewport above track)
    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "%d", section->currentPage + 1);
    int textWidth = renderer.getTextWidth(SMALL_FONT_ID, pageBuf);
    renderer.drawText(SMALL_FONT_ID, orientedMarginLeft + viewportWidth - textWidth, y - renderer.getLineHeight(SMALL_FONT_ID), pageBuf, 
                      SETTINGS.darkMode ? Color::LightGray : Color::DarkGray);
  }
}

void EpubReaderActivity::renderBookmarkIndicator() const {
  if (section && isPageBookmarked(currentSpineIndex, section->currentPage)) {
    const int sw = renderer.getScreenWidth();
    const int rw = 20;
    const int rh = 40;
    const int rx = sw - rw - 30;
    const int ry = 0;
    
    // Draw ribbon (inverted when in dark mode to stay visible)
    renderer.fillRect(rx, ry, rw, rh, !SETTINGS.darkMode);
  }
}

void EpubReaderActivity::saveBookmarks() const {
  FsFile f;
  if (Storage.openFileForWrite("ERS", epub->getCachePath() + "/bookmarks.bin", f)) {
    for (const auto& b : bookmarks) {
      uint8_t data[12]; // Increased to 12 bytes: 2+2+4+4(padding/ext) -> actually 2+2+4 = 8? Let's use 12 for safety/future
      data[0] = b.spineIndex & 0xFF;
      data[1] = (b.spineIndex >> 8) & 0xFF;
      data[2] = b.pageIndex & 0xFF;
      data[3] = (b.pageIndex >> 8) & 0xFF;
      
      // Store float progress (assuming IEEE 754 4-byte float)
      memcpy(&data[4], &b.progress, 4);
      
      // Zero out last 4 bytes for padding/future
      memset(&data[8], 0, 4);
      
      f.write(data, 12);
    }
    f.close();
  }
}

void EpubReaderActivity::loadBookmarks() {
  bookmarks.clear();
  FsFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/bookmarks.bin", f)) {
    const size_t fileSize = f.size();
    if (fileSize % 12 == 0) {
      // New format (12 bytes)
      uint8_t data[12];
      while (f.read(data, 12) == 12) {
        Bookmark b;
        b.spineIndex = data[0] | (data[1] << 8);
        b.pageIndex = data[2] | (data[3] << 8);
        memcpy(&b.progress, &data[4], 4);
        bookmarks.push_back(b);
      }
    } else if (fileSize % 4 == 0) {
      // Legacy format (4 bytes)
      uint8_t data[4];
      while (f.read(data, 4) == 4) {
        Bookmark b;
        b.spineIndex = data[0] | (data[1] << 8);
        b.pageIndex = data[2] | (data[3] << 8);
        b.progress = 0.0f; // Will be updated on toggle or render if needed? 
        // Actually, calculate best estimate for legacy
        if (epub) {
          b.progress = epub->calculateProgress(b.spineIndex, 0.0f);
        }
        bookmarks.push_back(b);
      }
    }
    f.close();
    std::sort(bookmarks.begin(), bookmarks.end(), [](const Bookmark& a, const Bookmark& b) {
      if (a.spineIndex != b.spineIndex) return a.spineIndex < b.spineIndex;
      return a.pageIndex < b.pageIndex;
    });
  }
}

void EpubReaderActivity::toggleBookmark() {
  if (!section) return;
  
  float bookProgress = 0.0f;
  if (epub && section->pageCount > 0) {
    const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress);
  }

  Bookmark current = { (uint16_t)currentSpineIndex, (uint16_t)section->currentPage, bookProgress };
  auto it = std::find(bookmarks.begin(), bookmarks.end(), current);
  if (it != bookmarks.end()) {
    bookmarks.erase(it);
  } else {
    bookmarks.push_back(current);
    std::sort(bookmarks.begin(), bookmarks.end(), [](const Bookmark& a, const Bookmark& b) {
      if (a.spineIndex != b.spineIndex) return a.spineIndex < b.spineIndex;
      return a.pageIndex < b.pageIndex;
    });
  }
  saveBookmarks();
  requestUpdate();
}

void EpubReaderActivity::nextBookmark() {
  if (bookmarks.empty() || !section) return;

  Bookmark current = { (uint16_t)currentSpineIndex, (uint16_t)section->currentPage };
  auto it = std::upper_bound(bookmarks.begin(), bookmarks.end(), current, [](const Bookmark& a, const Bookmark& b) {
    if (a.spineIndex != b.spineIndex) return a.spineIndex < b.spineIndex;
    return a.pageIndex < b.pageIndex;
  });

  Bookmark target;
  if (it == bookmarks.end()) {
    target = bookmarks[0];
  } else {
    target = *it;
  }

  if (currentSpineIndex != target.spineIndex || section->currentPage != target.pageIndex) {
    RenderLock lock(*this);
    currentSpineIndex = target.spineIndex;
    nextPageNumber = target.pageIndex;
    section.reset();
    requestUpdate();
  }
}

bool EpubReaderActivity::isPageBookmarked(int spine, int page) const {
  Bookmark target = { (uint16_t)spine, (uint16_t)page };
  return std::find(bookmarks.begin(), bookmarks.end(), target) != bookmarks.end();
}
