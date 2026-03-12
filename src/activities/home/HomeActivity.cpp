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
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // My Library, Recents, Plugins, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
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

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            // Only clear coverBmpPath if the book file itself is gone.
            // For transient failures (memory, read error) keep the path
            // so generation can succeed on the next fresh launch.
            if (!Storage.exists(book.path.c_str())) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
          }
          coverRendered = false;
          requestUpdate();
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              // Only clear coverBmpPath if the book file itself is gone.
              // Large XTC files may fail due to heap fragmentation after
              // reading — keep the path so generation retries on next launch.
              if (!Storage.exists(book.path.c_str())) {
                RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
                book.coverBmpPath = "";
              }
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
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

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  skipNextButtonCheck = true;

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
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
  if (skipNextButtonCheck) {
    if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) {
      skipNextButtonCheck = false;
    }
    return;
  }

  const int bookCount = recentBooks.size();
  const int menuCount = 4;

  auto getNextBookIdx = [](int cur, int total) {
    if (total <= 1) return 0;
    return (cur + 1) % total;
  };
  
  auto getPrevBookIdx = [](int cur, int total) {
    if (total <= 1) return 0;
    return (cur + total - 1) % total;
  };

  // Per user request:
  // [ 2 1 3 ] -> Right -> [ 1 3 4 ]
  // [ B2 B1 B3 ] -> Left -> [ B4 B2 B1 ]

  // Side buttons (usually physical 4 and 5) - Strictly for book covers
  if (mappedInput.wasPressedRaw(HalGPIO::BTN_UP) || mappedInput.wasPressedRaw(4)) {
    if (bookCount > 0) {
      focusZone = Zone::BOOKS;
      bookSelectorIndex = getNextBookIdx(bookSelectorIndex, bookCount);
      coverBufferStored = false;
      coverRendered = false;  // Force full re-render from SD for new selection
      requestUpdate();
    }
  }
  if (mappedInput.wasPressedRaw(HalGPIO::BTN_DOWN) || mappedInput.wasPressedRaw(5)) {
    if (bookCount > 0) {
      focusZone = Zone::BOOKS;
      bookSelectorIndex = getPrevBookIdx(bookSelectorIndex, bookCount);
      coverBufferStored = false;
      coverRendered = false;  // Force full re-render from SD for new selection
      requestUpdate();
    }
  }

  // Power button short press = Confirm (when configured as PAGE_TURN)
  const bool powerConfirm = (SETTINGS.shortPwrBtn == CrossPointSettings::PAGE_TURN) &&
                             mappedInput.wasShortPressedRaw(HalGPIO::BTN_POWER, SETTINGS.getPowerButtonDuration());

  // Front buttons (Logical 1-to-1 mapping)

  // Button 2 (Confirm) - Selection
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || powerConfirm) {
    int idx = 0;
    const int myLibraryIdx = idx++;
    const int recentsIdx = idx++;
    const int pluginsIdx = idx++;
    const int settingsIdx = idx++;

    if (focusZone == Zone::BOOKS && !recentBooks.empty()) {
      onSelectBook(recentBooks[bookSelectorIndex].path);
    } else if (focusZone == Zone::MENU) {
      if (menuSelectorIndex == myLibraryIdx) {
        onMyLibraryOpen();
      } else if (menuSelectorIndex == recentsIdx) {
        onRecentsOpen();
      } else if (menuSelectorIndex == pluginsIdx) {
        onPluginsOpen();
      } else if (menuSelectorIndex == settingsIdx) {
        onSettingsOpen();
      }
    }
    return;
  }

  // Button 3 (Up) - Vertical movement
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (focusZone == Zone::MENU) {
      if (menuSelectorIndex > 0) {
        menuSelectorIndex--;
      } else {
        // Move focus up to books zone
        focusZone = Zone::BOOKS;
      }
      requestUpdate();
    } else if (focusZone == Zone::BOOKS) {
      // Loop to bottom of menu
      focusZone = Zone::MENU;
      menuSelectorIndex = menuCount - 1;
      requestUpdate();
    }
  }

  // Button 4 (Down) - Vertical movement
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (focusZone == Zone::BOOKS) {
      // Move focus down to menu zone
      focusZone = Zone::MENU;
      menuSelectorIndex = 0;
      requestUpdate();
    } else if (focusZone == Zone::MENU) {
      if (menuSelectorIndex < menuCount - 1) {
        menuSelectorIndex++;
        requestUpdate();
      } else {
        // Loop back to books
        focusZone = Zone::BOOKS;
        requestUpdate();
      }
    }
  }

  // Button 1 (Back) - Default Home behavior
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // On home, Back usually doesn't do much unless it exits popups.
    // For now, no-op or specific home action.
  }
}

void HomeActivity::render(Activity::RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

  // Calculate a compatible selectorIndex for legacy themes
  // For FlowTheme, we pass (1000 + bookSelectorIndex) when focus is on menu to retain state
  int compatibleSelectorIndex = -1;
  if (focusZone == Zone::BOOKS) {
    compatibleSelectorIndex = bookSelectorIndex;
  } else {
    compatibleSelectorIndex = 1000 + bookSelectorIndex;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, compatibleSelectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this),
                          labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_RECENTS),
                                        "Plugins", tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Game, Settings};

  // Add 50px extra spacing below books (+50) for better visual separation
  int menuY = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing + 50;
  GUI.drawButtonMenu(
      renderer,
      Rect{0, menuY, pageWidth,
           pageHeight - menuY - metrics.verticalSpacing},
      static_cast<int>(menuItems.size()), focusZone == Zone::MENU ? menuSelectorIndex : -1,
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight); // 294 or 314 for Flow
  }
}
