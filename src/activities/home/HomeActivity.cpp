#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "LibraryStore.h"
#include "MappedInputManager.h"
#include "PathRepairManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"
#include "util/TimeService.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // My Library, Recents, Plugins, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  RECENT_BOOKS.cleanupMissingBooks();
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  // Ensure cache directories exist before trying to write thumbnails.
  // After ClearCacheActivity runs, directories may have been deleted.
  LIBRARY_STORE.ensureCacheDirectories();

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (abortLoading) break;

    std::string coverPath = book.coverBmpPath.empty() ? "" : UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    bool needGeneration = coverPath.empty() || !Storage.exists(coverPath.c_str());

    if (needGeneration) {
      if (StringUtils::checkFileExtension(book.path, ".epub")) {
        Epub epub(book.path, "/.crosspoint");
        if (epub.load(true, true)) {
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool ok = epub.generateThumbBmp(coverHeight);
          if (!ok && !Storage.exists(book.path.c_str())) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "", book.fileSize);
            book.coverBmpPath = "";
          }
          coverRendered = false;
          coverBufferStored = false;  // Discard stale buffer so next render draws fresh
          requestUpdate();
        }
      } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                 StringUtils::checkFileExtension(book.path, ".xtc")) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load()) {
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool ok = xtc.generateThumbBmp(coverHeight);
          if (!ok && !Storage.exists(book.path.c_str())) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "", book.fileSize);
            book.coverBmpPath = "";
          }
          coverRendered = false;
          coverBufferStored = false;  // Discard stale buffer so next render draws fresh
          requestUpdate();
        }
      }
    }
    progress++;
    vTaskDelay(1);
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  bookSelectorIndex = 0;
  menuSelectorIndex = 0;
  focusZone = Zone::BOOKS;
  firstRenderDone = false;
  recentsLoaded   = false;
  recentsLoading  = false;
  coverRendered = false;
  coverBufferStored = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  skipNextButtonCheck = true;

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  abortLoading = true;
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  if (themeSwitcher.isVisible()) {
      if (themeSwitcher.handleInput(mappedInput)) {
          // Theme was changed and confirmed
          requestUpdate();
      } else {
          // Theme selection changed (Redraw)
          requestUpdate();
      }
      return;
  }

  if (skipNextButtonCheck) {
    if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) {
      skipNextButtonCheck = false;
    }
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int menuCount = 4;

  // Power button short press = Confirm (when configured as PAGE_TURN)
  const bool powerConfirm = (SETTINGS.shortPwrBtn == CrossPointSettings::PAGE_TURN) &&
                             mappedInput.wasShortPressedRaw(HalGPIO::BTN_POWER, SETTINGS.getPowerButtonDuration());

  // 1. Side Buttons (Physical 4 & 5 / BTN_UP & BTN_DOWN) - Strictly for Book Selection
  if (mappedInput.wasPressedRaw(HalGPIO::BTN_UP) || mappedInput.wasPressedRaw(4)) {
    if (bookCount > 0) {
      focusZone = Zone::BOOKS;
      bookSelectorIndex = (bookSelectorIndex + 1) % bookCount;
      requestUpdate();
    }
  }
  if (mappedInput.wasPressedRaw(HalGPIO::BTN_DOWN) || mappedInput.wasPressedRaw(5)) {
    if (bookCount > 0) {
      focusZone = Zone::BOOKS;
      bookSelectorIndex = (bookSelectorIndex + bookCount - 1) % bookCount;
      requestUpdate();
    }
  }

  // 2. Front Buttons (Button 3 & 4 / Left & Right) - Strictly for Menu Selection
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (focusZone == Zone::BOOKS) {
      focusZone = Zone::MENU;
      menuSelectorIndex = 3; // Start at Settings
    } else {
      menuSelectorIndex = (menuSelectorIndex + menuCount - 1) % menuCount;
    }
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (focusZone == Zone::BOOKS) {
      focusZone = Zone::MENU;
      menuSelectorIndex = 0; // Start at Library
    } else {
      menuSelectorIndex = (menuSelectorIndex + 1) % menuCount;
    }
    requestUpdate();
  }

  // 3. Confirm Button (Button 2)
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || powerConfirm) {
    if (focusZone == Zone::BOOKS && !recentBooks.empty()) {
      freeCoverBuffer();
      onSelectBook(recentBooks[bookSelectorIndex].path);
    } else if (focusZone == Zone::MENU) {
      freeCoverBuffer();
      if (menuSelectorIndex == 0) onMyLibraryOpen();
      else if (menuSelectorIndex == 1) onRecentsOpen();
      else if (menuSelectorIndex == 2) onPluginsOpen();
      else if (menuSelectorIndex == 3) onSettingsOpen();
    }
    return;
  }

  // Back Button (Button 1) - Toggles Theme Switcher Overlay
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    themeSwitcher.show();
    requestUpdate();
    return;
  }
}

void HomeActivity::render(Activity::RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Calculate compatible index for drawing (FlowTheme, etc.)
  int compatibleSelectorIndex = (focusZone == Zone::BOOKS) ? bookSelectorIndex : (1000 + bookSelectorIndex);

  const auto labels = mappedInput.mapLabels(BaseTheme::HINT_BACK, BaseTheme::HINT_OK, BaseTheme::HINT_PREV, BaseTheme::HINT_NEXT);
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, compatibleSelectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this),
                          labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_RECENTS),
                                        tr(STR_PLUGINS), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Game, Settings};

  // Add 50px extra spacing below books (+50) for better visual separation
  int menuY = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing + 30;
  GUI.drawButtonMenu(
      renderer,
      Rect{0, menuY, pageWidth,
           pageHeight - menuY - metrics.verticalSpacing},
      static_cast<int>(menuItems.size()), focusZone == Zone::MENU ? menuSelectorIndex : -1,
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  // Draw Theme Switcher Overlay on top
  themeSwitcher.render(renderer);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}
