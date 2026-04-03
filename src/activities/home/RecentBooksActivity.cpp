#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "PathRepairManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"
#include <Epub.h>
#include <Xtc.h>
#include "components/icons/book.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;

void maskCorners(const GfxRenderer& renderer, int x, int y, int w, int h, int r) {
  for (int dy = 0; dy < r; dy++) {
    for (int dx = 0; dx < r; dx++) {
      if ((r - dx) * (r - dx) + (r - dy) * (r - dy) > r * r) {
        renderer.drawPixel(x + dx, y + dy, false);                  // TL
        renderer.drawPixel(x + w - 1 - dx, y + dy, false);          // TR
        renderer.drawPixel(x + dx, y + h - 1 - dy, false);          // BL
        renderer.drawPixel(x + w - 1 - dx, y + h - 1 - dy, false);  // BR
      }
    }
  }
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  // Remove entries whose files are gone and delete their cache dirs
  RECENT_BOOKS.cleanupMissingBooks();

  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  const int maxBooks = BOOKS_PER_PAGE * 2;  // Up to 2 pages (18 books)
  recentBooks.reserve(std::min((int)books.size(), maxBooks));

  for (const auto& book : books) {
    if ((int)recentBooks.size() >= maxBooks) break;
    recentBooks.push_back(book);
  }
  invalidateCache();
}

void RecentBooksActivity::loadPageCovers(int pageStart, int coverHeight) {
  if (recentsLoading) return;
  recentsLoading = true;

  const int pageEnd = std::min(pageStart + BOOKS_PER_PAGE, static_cast<int>(recentBooks.size()));
  
  // First, check if we even need to show a popup. 
  // If all thumbnails on this page exist, we don't need to block.
  bool needsGeneration = false;
  for (int i = pageStart; i < pageEnd; ++i) {
    if (recentBooks[i].coverBmpPath.empty()) continue;
    std::string thumbPath = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, coverHeight);
    if (!Storage.exists(thumbPath.c_str())) {
      needsGeneration = true;
      break;
    }
  }

  if (!needsGeneration) {
    lastLoadedPageStart = pageStart;
    recentsLoading = false;
    return;
  }

  bool showingLoading = false;
  Rect popupRect;
  int processedCount = 0;
  const int totalToProcess = pageEnd - pageStart;

  for (int i = pageStart; i < pageEnd; ++i) {
    RecentBook& book = recentBooks[i];
    
    // Always check if thumbnail exists, if not, try background generation
    std::string coverPath = book.coverBmpPath.empty() ? "" : UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          Epub epub(book.path, "/.crosspoint");
          // load(true, true) ensures metadata is re-indexed from ZIP if cache is missing
          if (epub.load(true, true)) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + processedCount * (90 / totalToProcess));
            
            bool success = epub.generateThumbBmp(coverHeight);
            if (!success && !Storage.exists(book.path.c_str())) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "", book.fileSize);
              book.coverBmpPath = "";
            }
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
            GUI.fillPopupProgress(renderer, popupRect, 10 + processedCount * (90 / totalToProcess));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success && !Storage.exists(book.path.c_str())) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "", book.fileSize);
              book.coverBmpPath = "";
            }
            requestUpdate();
          }
        }
    }
    processedCount++;
    vTaskDelay(1); // Yield for each item on page
  }

  lastLoadedPageStart = pageStart;
  recentsLoading = false;
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();
  loadRecentBooks();
  selectorIndex = 0;
  invalidateCache();
  skipNextButtonCheck = true;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
}

void RecentBooksActivity::deleteSelectedBook() {
  if (recentBooks.empty() || selectorIndex >= static_cast<int>(recentBooks.size())) return;

  Storage.remove(recentBooks[selectorIndex].path.c_str());
  RECENT_BOOKS.cleanupMissingBooks();

  loadRecentBooks();
  if (!recentBooks.empty() && selectorIndex >= static_cast<int>(recentBooks.size())) {
    selectorIndex = static_cast<int>(recentBooks.size()) - 1;
  }
  menuState = MenuState::None;
  lastLoadedPageStart = -1;
  requestUpdate();
}

void RecentBooksActivity::renderDeleteMenu() const {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int mw = 280, mh = 130;
  const int mx = (sw - mw) / 2, my = (sh - mh) / 2;

  renderer.fillRoundedRect(mx, my, mw, mh, 10, Color::White);
  renderer.drawRoundedRect(mx, my, mw, mh, 2, 10, true);

  const char* opts[] = {"Delete File", "Cancel"};
  for (int i = 0; i < 2; i++) {
    const int ry = my + 20 + i * 50;
    if (menuSelectedIndex == i) {
      renderer.fillRoundedRect(mx + 10, ry - 5, mw - 20, 38, 6, Color::Black);
    }
    renderer.drawText(UI_12_FONT_ID, mx + 20, ry + 2, opts[i], menuSelectedIndex != i);
  }
}

void RecentBooksActivity::renderConfirmDialog() const {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int mw = 280, mh = 160;
  const int mx = (sw - mw) / 2, my = (sh - mh) / 2;

  renderer.fillRoundedRect(mx, my, mw, mh, 10, Color::White);
  renderer.drawRoundedRect(mx, my, mw, mh, 2, 10, true);
  renderer.drawText(UI_12_FONT_ID, mx + 20, my + 18, "Delete this file?");

  const char* opts[] = {"Yes", "No"};
  for (int i = 0; i < 2; i++) {
    const int ry = my + 60 + i * 50;
    if (menuSelectedIndex == i) {
      renderer.fillRoundedRect(mx + 10, ry - 5, mw - 20, 38, 6, Color::Black);
    }
    renderer.drawText(UI_12_FONT_ID, mx + 20, ry + 2, opts[i], menuSelectedIndex != i);
  }
}

void RecentBooksActivity::loop() {
  if (skipNextButtonCheck) {
    if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // --- Delete menu state ---
  if (menuState == MenuState::Delete || menuState == MenuState::Confirm) {
    const int optCount = 2;
    if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      menuSelectedIndex = (menuSelectedIndex + 1) % optCount;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
        mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      menuSelectedIndex = (menuSelectedIndex + optCount - 1) % optCount;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (menuState == MenuState::Delete) {
        if (menuSelectedIndex == 0) {  // Delete
          menuState = MenuState::Confirm;
          menuSelectedIndex = 1;  // Default to No (safer)
          requestUpdate();
        } else {  // Cancel
          menuState = MenuState::None;
          requestUpdate();
        }
      } else {  // Confirm
        if (menuSelectedIndex == 0) {  // Yes
          deleteSelectedBook();
        } else {  // No
          menuState = MenuState::None;
          requestUpdate();
        }
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      menuState = MenuState::None;
      requestUpdate();
    }
    return;
  }

  // Long press Confirm (600ms) opens delete menu
  if (!recentBooks.empty() &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 600)) {
    menuState = MenuState::Delete;
    menuSelectedIndex = 1;  // Default to Cancel (safer)
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < static_cast<int>(recentBooks.size())) {
      LOG_DBG("RBA", "Selected recent book: %s", recentBooks[selectorIndex].path.c_str());
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  }

  int listSize = static_cast<int>(recentBooks.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  // Fast forward jumps by a row (3 items)
  buttonNavigator.onNextContinuous([this, listSize] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, 3);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, 3);
    requestUpdate();
  });
}

void RecentBooksActivity::updatePageCache(int pageStart, int count, int coverHeight) {
  if (cachedPageStart == pageStart && (int)pageCache.size() == count) return;

  pageCache.clear();
  pageCache.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int bookIdx = pageStart + i;
    const auto& book = recentBooks[bookIdx];
    ItemRenderCache item;
    if (!book.coverBmpPath.empty()) {
      item.thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      item.hasThumb = Storage.exists(item.thumbPath.c_str());
    }
    pageCache.push_back(std::move(item));
  }
  cachedPageStart = pageStart;
}

void RecentBooksActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int gridTopOffset = 20;
  
  // Calculate grid layout sizes
  const int columns     = 3;
  const int coverHeight = 180; // Unified height for thumbnails
  const int coverWidth  = 123; // User requested fixed width
  const int rowSpacing  = metrics.verticalSpacing + 15;
  const int totalGridWidth = (columns * coverWidth) + ((columns - 1) * metrics.verticalSpacing);
  const int startXOffset   = (pageWidth - totalGridWidth) / 2;

  // Pagination
  const int totalBooks  = static_cast<int>(recentBooks.size());
  const int totalPages  = (totalBooks + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
  const int currentPage = (totalPages > 0) ? (selectorIndex / BOOKS_PER_PAGE) : 0;
  const int pageStart   = currentPage * BOOKS_PER_PAGE;
  const int pageCount   = std::min(BOOKS_PER_PAGE, totalBooks - pageStart);

  // Recent tab grid
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    updatePageCache(pageStart, pageCount, coverHeight);

    for (int i = 0; i < pageCount; ++i) {
      const auto& cacheItem = pageCache[i];
      const int bookIdx = pageStart + i;
      const int col = i % columns;
      const int row = i / columns;

      const int x = startXOffset + col * (coverWidth + metrics.verticalSpacing);
      const int y = contentTop + gridTopOffset + row * (coverHeight + rowSpacing);

      // Draw cover image or fallback icon
      bool drawn = false;
      if (cacheItem.hasThumb) {
        FsFile file;
        if (Storage.openFileForRead("HOME", cacheItem.thumbPath, file)) {
          Bitmap bmp(file);
          if (bmp.parseHeaders() == BmpReaderError::Ok) {
            renderer.drawBitmap(bmp, x + (coverWidth - bmp.getWidth()) / 2, y + (coverHeight - bmp.getHeight()) / 2,
                                bmp.getWidth(), bmp.getHeight());
            // Mask sharp corners of the bitmap so they don't leak outside the rounded border
            maskCorners(renderer, x + (coverWidth - bmp.getWidth()) / 2, y + (coverHeight - bmp.getHeight()) / 2,
                        bmp.getWidth(), bmp.getHeight(), 4);
            renderer.drawRoundedRect(x, y, coverWidth, coverHeight, 1, 4, true);
            drawn = true;
          }
          file.close();
        }
      }

      if (!drawn) {
        renderer.drawRoundedRect(x, y, coverWidth, coverHeight, 1, 4, true);
        renderer.fillRoundedRect(x + 1, y + 1, coverWidth - 2, coverHeight - 2, 4, Color::White);
        renderer.drawIcon(BookIcon, x + (coverWidth - 32) / 2, y + (coverHeight - 32) / 2, 32, 32);
      }

      // Selection box — 2px rounded rect, same as CoverTheme
      if (bookIdx == selectorIndex) {
        renderer.drawRoundedRect(x - 2, y - 2, coverWidth + 4, coverHeight + 4, 3, 5, true);
      }
    }

    // Page indicator (dots)
    if (totalPages > 1) {
      const int dotSize = 8;
      const int dotSpacing = 8;
      const int totalDotWidth = (totalPages * dotSize) + ((totalPages - 1) * dotSpacing);
      const int startX = (pageWidth - totalDotWidth) / 2;
      const int dotY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 4;

      for (int p = 0; p < totalPages; p++) {
        int x = startX + p * (dotSize + dotSpacing);
        if (p == currentPage) {
          renderer.fillRect(x, dotY, dotSize, dotSize, true);
        } else {
          renderer.drawRect(x, dotY, dotSize, dotSize, true);
        }
      }
    }
  }

  const auto labels = mappedInput.mapLabels(BaseTheme::HINT_BACK, BaseTheme::HINT_OK, BaseTheme::HINT_PREV, BaseTheme::HINT_NEXT);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (menuState == MenuState::Delete)  renderDeleteMenu();
  if (menuState == MenuState::Confirm) renderConfirmDialog();

  renderer.displayBuffer();

  if (lastLoadedPageStart != pageStart) {
    loadPageCovers(pageStart, coverHeight);
  }
}
