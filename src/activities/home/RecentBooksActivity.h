#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class RecentBooksActivity final : public Activity {
 private:
  static constexpr int BOOKS_PER_PAGE = 9;

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  std::vector<RecentBook> recentBooks;

  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onGoHome;

  bool skipNextButtonCheck = false;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;

  // Data loading
  void loadRecentBooks();
  void loadRecentCovers(int coverHeight);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::function<void()>& onGoHome,
                               const std::function<void(const std::string& path)>& onSelectBook)
      : Activity("RecentBooks", renderer, mappedInput), onSelectBook(onSelectBook), onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
