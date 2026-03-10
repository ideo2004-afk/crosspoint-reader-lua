#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"
#include <Epub.h>
#include <Xtc.h>
#include "components/icons/book.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  // Remove entries whose files are gone and delete their cache dirs
  RECENT_BOOKS.cleanupMissingBooks();

  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  const int maxBooks = BOOKS_PER_PAGE * 4;  // Up to 4 pages
  recentBooks.reserve(std::min((int)books.size(), maxBooks));

  for (const auto& book : books) {
    if ((int)recentBooks.size() >= maxBooks) break;
    recentBooks.push_back(book);
  }
}

void RecentBooksActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          // If epub, try to load the metadata for title/author and cover
          Epub epub(book.path, "/.crosspoint");
          epub.load(false, true); // Skip loading css since we only need metadata here

          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          requestUpdate();
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
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

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  // Load data
  loadRecentBooks();

  selectorIndex = 0;
  skipNextButtonCheck = true;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
}

void RecentBooksActivity::loop() {
  if (skipNextButtonCheck) {
    if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) {
      skipNextButtonCheck = false;
    }
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

void RecentBooksActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int gridTopOffset = 20;
  
  // Calculate grid layout sizes
  const int columns = 3;
  const int coverWidth = (pageWidth - (metrics.contentSidePadding * 2) - (metrics.verticalSpacing * (columns - 1))) / columns;
  // Preserve rough 3:4 aspect ratio for covers
  const int coverHeight = (coverWidth * 4) / 3;
  const int rowSpacing  = metrics.verticalSpacing + 15;

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
    for (int i = 0; i < pageCount; ++i) {
      const int bookIdx = pageStart + i;
      const int col = i % columns;
      const int row = i / columns;

      const int x = metrics.contentSidePadding + col * (coverWidth + metrics.verticalSpacing);
      const int y = contentTop + gridTopOffset + row * (coverHeight + rowSpacing);

      Rect coverRect(x, y, coverWidth, coverHeight);

      // Draw cover image or fallback icon
      if (!recentBooks[bookIdx].coverBmpPath.empty()) {
        std::string coverPath = UITheme::getCoverThumbPath(recentBooks[bookIdx].coverBmpPath, coverHeight);
        if (Storage.exists(coverPath.c_str())) {
          FsFile file;
          if (Storage.openFileForRead("HOME", coverPath, file)) {
            Bitmap bmp(file);
            if (bmp.parseHeaders() == BmpReaderError::Ok) {
              renderer.drawBitmap(bmp, x + (coverWidth - bmp.getWidth()) / 2, y + (coverHeight - bmp.getHeight()) / 2,
                                  bmp.getWidth(), bmp.getHeight());
            }
            file.close();
          }
        } else {
          renderer.drawIcon(BookIcon, x + (coverWidth - 32) / 2, y + (coverHeight - 32) / 2, 32, 32);
        }
      } else {
        renderer.drawIcon(BookIcon, x + (coverWidth - 32) / 2, y + (coverHeight - 32) / 2, 32, 32);
      }

      // Selection box
      if (bookIdx == selectorIndex) {
        renderer.drawRect(coverRect.x - 4, coverRect.y - 4, coverRect.width + 8, coverRect.height + 8, true);
      }
    }

    // Page indicator  e.g. "2 / 4"
    if (totalPages > 1) {
      char pageStr[12];
      snprintf(pageStr, sizeof(pageStr), "%d / %d", currentPage + 1, totalPages);
      const int tw = renderer.getTextWidth(SMALL_FONT_ID, pageStr);
      const int ty = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 16;
      renderer.drawText(SMALL_FONT_ID, (pageWidth - tw) / 2, ty, pageStr);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  
  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(coverHeight);
  }
}
