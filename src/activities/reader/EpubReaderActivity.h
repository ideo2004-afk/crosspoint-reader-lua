#pragma once
#include <Epub.h>
#include <Epub/Section.h>

#include "EpubReaderMenuActivity.h"
#include "activities/ActivityWithSubactivity.h"

class EpubReaderActivity final : public ActivityWithSubactivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  bool pendingSubactivityExit = false;  // Defer subactivity exit to avoid use-after-free
  bool pendingGoHome = false;           // Defer go home to avoid race condition with display task
  bool pendingScreenshot = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  uint32_t sessionStartMillis = 0;
  
  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;
  bool inMenu = false;
  int menuSelectedIndex = 0;
  bool inScrubber = false;
  int scrubberPercent = 0;

  struct Bookmark {
    uint16_t spineIndex;
    uint16_t pageIndex;
    float progress;
    bool operator==(const Bookmark& other) const {
      return spineIndex == other.spineIndex && pageIndex == other.pageIndex;
    }
  };
  std::vector<Bookmark> bookmarks;

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;
  void renderBookmarkIndicator() const;
  void renderMenu() const;
  void saveProgress(int spineIndex, int currentPage, int pageCount);
  void saveBookmarks() const;
  void loadBookmarks();
  void toggleBookmark();
  void nextBookmark();
  bool isPageBookmarked(int spine, int page) const;
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void jumpPercent(int deltaPercent);
  void onReaderMenuBack(uint8_t orientation);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                              const std::function<void()>& onGoBack, const std::function<void()>& onGoHome)
      : ActivityWithSubactivity("EpubReader", renderer, mappedInput),
        epub(std::move(epub)),
        onGoBack(onGoBack),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
