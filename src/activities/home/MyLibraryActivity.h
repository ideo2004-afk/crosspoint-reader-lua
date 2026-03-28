#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class MyLibraryActivity final : public Activity {
 private:
  enum class MenuState { None, Delete, Confirm };

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  bool skipNextButtonCheck = false;

  MenuState menuState = MenuState::None;
  int menuSelectedIndex = 0;  // 0=Delete, 1=Cancel / 0=Yes, 1=No

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;

  // Callbacks
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onGoHome;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

  void deleteSelectedFile();
  void renderDeleteMenu() const;
  void renderConfirmDialog() const;

 public:
  explicit MyLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& onGoHome,
                             const std::function<void(const std::string& path)>& onSelectBook,
                             std::string initialPath = "/books")
      : Activity("MyLibrary", renderer, mappedInput),
        basepath(initialPath.empty() ? "/books" : std::move(initialPath)),
        onSelectBook(onSelectBook),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
