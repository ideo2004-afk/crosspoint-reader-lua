#include "MyLibraryActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "LibraryStore.h"
#include "util/StringUtils.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    // Directories first
    bool isDir1 = str1.back() == '/';
    bool isDir2 = str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    // Start naive natural sort
    const char* s1 = str1.c_str();
    const char* s2 = str2.c_str();

    // Iterate while both strings have characters
    while (*s1 && *s2) {
      // Check if both are at the start of a number
      if (isdigit(*s1) && isdigit(*s2)) {
        // Skip leading zeros and track them
        const char* start1 = s1;
        const char* start2 = s2;
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;

        // Count digits to compare lengths first
        int len1 = 0, len2 = 0;
        while (isdigit(s1[len1])) len1++;
        while (isdigit(s2[len2])) len2++;

        // Different length so return smaller integer value
        if (len1 != len2) return len1 < len2;

        // Same length so compare digit by digit
        for (int i = 0; i < len1; i++) {
          if (s1[i] != s2[i]) return s1[i] < s2[i];
        }

        // Numbers equal so advance pointers
        s1 += len1;
        s2 += len2;
      } else {
        // Regular case-insensitive character comparison
        char c1 = tolower(*s1);
        char c2 = tolower(*s2);
        if (c1 != c2) return c1 < c2;
        s1++;
        s2++;
      }
    }

    // One string is prefix of other
    return *s1 == '\0' && *s2 != '\0';
  });
}
}  // namespace

void MyLibraryActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      auto filename = std::string(name);
      if (StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtch") ||
          StringUtils::checkFileExtension(filename, ".xtc") || StringUtils::checkFileExtension(filename, ".txt") ||
          StringUtils::checkFileExtension(filename, ".md") || StringUtils::checkFileExtension(filename, ".bmp")) {
        files.emplace_back(filename);
      }
    }
    file.close();
  }
  root.close();
  sortFileList(files);
  LIBRARY_STORE.scanFolder(basepath);
}

void MyLibraryActivity::onEnter() {
  Activity::onEnter();

  loadFiles();
  selectorIndex = 0;
  skipNextButtonCheck = true;
  requestUpdate();
}

void MyLibraryActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void MyLibraryActivity::loop() {
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
          deleteSelectedFile();
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

  // Long press BACK (1s+) goes to books root
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/books") {
    basepath = "/books";
    loadFiles();
    selectorIndex = 0;
    return;
  }

  // Long press Confirm (600ms) opens delete menu (files only, not directories)
  if (!files.empty() && files[selectorIndex].back() != '/' &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 600)) {
    menuState = MenuState::Delete;
    menuSelectedIndex = 1;  // Default to Cancel (safer)
    requestUpdate();
    return;
  }

  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (files.empty()) {
      return;
    }

    if (basepath.back() != '/') basepath += "/";
    if (files[selectorIndex].back() == '/') {
      basepath += files[selectorIndex].substr(0, files[selectorIndex].length() - 1);
      loadFiles();
      selectorIndex = 0;
      requestUpdate();
    } else {
      onSelectBook(basepath + files[selectorIndex]);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/books") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath == "/books" || basepath.empty()) basepath = "/books";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else {
        onGoHome();
      }
    }
  }

  int listSize = static_cast<int>(files.size());
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void MyLibraryActivity::deleteSelectedFile() {
  if (files.empty() || selectorIndex >= files.size()) return;
  std::string prefix = basepath;
  if (prefix.back() != '/') prefix += "/";
  std::string fullPath = prefix + files[selectorIndex];

  Storage.remove(fullPath.c_str());
  LIBRARY_STORE.scanFolder(basepath); // Update index after deletion
  RECENT_BOOKS.cleanupMissingBooks();

  loadFiles();
  if (!files.empty() && selectorIndex >= files.size()) {
    selectorIndex = files.size() - 1;
  }
  menuState = MenuState::None;
  requestUpdate();
}

void MyLibraryActivity::renderDeleteMenu() const {
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

void MyLibraryActivity::renderConfirmDialog() const {
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

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    return filename.substr(0, filename.length() - 1);
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

void MyLibraryActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/books") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_BOOKS_FOUND));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return getFileName(files[index]); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index]); });

    const int pageItems   = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
    const int totalFiles  = static_cast<int>(files.size());
    const int totalPages  = (pageItems > 0) ? (totalFiles + pageItems - 1) / pageItems : 1;
    // Page number display removed per user request (redundant with scroll bar)
  }

  const auto labels = mappedInput.mapLabels(BaseTheme::HINT_BACK, BaseTheme::HINT_OK, BaseTheme::HINT_PREV, BaseTheme::HINT_NEXT);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (menuState == MenuState::Delete)  renderDeleteMenu();
  if (menuState == MenuState::Confirm) renderConfirmDialog();

  renderer.displayBuffer();
}

size_t MyLibraryActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}