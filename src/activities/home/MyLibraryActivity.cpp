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
#include "Epub.h"
#include "Xtc.h"
#include "Bitmap.h"

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

  // --- Menu States (ContextMenu / ConfirmDelete) ---
  if (menuState != MenuState::None) {
    int optCount = (menuState == MenuState::ContextMenu) ? 3 : 2;
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
      if (menuState == MenuState::ContextMenu) {
        if (menuSelectedIndex == 0) { // Toggle View Mode
          viewMode = (viewMode == ViewMode::Grid) ? ViewMode::List : ViewMode::Grid;
          menuState = MenuState::None;
          selectorIndex = 0; // Reset index when switching modes for simplicity
        } else if (menuSelectedIndex == 1) { // Delete
          menuState = MenuState::ConfirmDelete;
          menuSelectedIndex = 1; // Default to "No"
        } else { // Cancel
          menuState = MenuState::None;
        }
      } else if (menuState == MenuState::ConfirmDelete) {
        if (menuSelectedIndex == 0) deleteSelectedFile();
        else menuState = MenuState::None;
      }
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      menuState = MenuState::None;
      requestUpdate();
    }
    return;
  }

  // --- Long Press Detection on Confirm (Context Menu) ---
  if (!files.empty() && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 600)) {
    menuState = MenuState::ContextMenu;
    menuSelectedIndex = 0;
    requestUpdate();
    return;
  }

  // --- Normal Navigation ---
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (files.empty()) return;

    std::string prefix = basepath;
    if (prefix.back() != '/') prefix += "/";
    
    if (files[selectorIndex].back() == '/') {
      basepath = prefix + files[selectorIndex].substr(0, files[selectorIndex].length() - 1);
      loadFiles();
      selectorIndex = 0;
      requestUpdate();
    } else {
      onSelectBook(prefix + files[selectorIndex]);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (basepath != "/books") {
      const std::string oldPath = basepath;
      size_t lastSlash = basepath.find_last_of('/');
      basepath = basepath.substr(0, lastSlash);
      if (basepath == "/books" || basepath.empty()) basepath = "/books";
      
      loadFiles();
      // Select the directory we just came out of
      size_t lastSlashOld = oldPath.find_last_of('/');
      std::string dirName = oldPath.substr(lastSlashOld + 1) + "/";
      selectorIndex = findEntry(dirName);
      
      requestUpdate();
    } else {
      onGoHome();
    }
  }

  int listSize = static_cast<int>(files.size());
  int itemsPerPage = (viewMode == ViewMode::Grid) ? 9 : UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, itemsPerPage] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, itemsPerPage);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, itemsPerPage] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, itemsPerPage);
    requestUpdate();
  });
}

void MyLibraryActivity::deleteSelectedFile() {
  if (files.empty() || selectorIndex >= files.size()) return;
  
  std::string prefix = basepath;
  if (prefix.back() != '/') prefix += "/";
  std::string fullPath = prefix + files[selectorIndex];

  if (files[selectorIndex].back() == '/') {
    Storage.removeDir(fullPath.c_str());
  } else {
    Storage.remove(fullPath.c_str());
  }
  
  LIBRARY_STORE.scanFolder(basepath);
  RECENT_BOOKS.cleanupMissingBooks();

  loadFiles();
  if (!files.empty() && selectorIndex >= files.size()) {
    selectorIndex = files.size() - 1;
  }
  menuState = MenuState::None;
  requestUpdate();
}

void MyLibraryActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/books") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 20, tr(STR_NO_BOOKS_FOUND));
  } else {
    if (viewMode == ViewMode::Grid) {
      renderGallery();
    } else {
      renderList();
    }
  }

  const auto labels = mappedInput.mapLabels(BaseTheme::HINT_BACK, BaseTheme::HINT_OK, BaseTheme::HINT_PREV, BaseTheme::HINT_NEXT);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (menuState == MenuState::ContextMenu) renderContextMenu();
  if (menuState == MenuState::ConfirmDelete) renderConfirmDelete();

  renderer.displayBuffer();
}

void MyLibraryActivity::renderList() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
      [this](int index) { 
        std::string name = files[index];
        if (name.back() == '/') return name.substr(0, name.length() - 1);
        auto pos = name.rfind('.');
        return (pos != std::string::npos) ? name.substr(0, pos) : name;
      }, nullptr,
      [this](int index) { return UITheme::getFileIcon(files[index]); });
}

void MyLibraryActivity::renderGallery() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  
  const int columns = 3;
  const int rows = 3;
  const int itemsPerPage = columns * rows;
  const int currentPage = selectorIndex / itemsPerPage;
  const int pageStart = currentPage * itemsPerPage;
  
  const int contentTop = metrics.topPadding + metrics.headerHeight + 20;
  const int horizontalPadding = metrics.contentSidePadding;
  const int spacingX = 20;
  const int spacingY = 40;
  const int coverW = (pageWidth - (horizontalPadding * 2) - (spacingX * (columns - 1))) / columns;
  const int coverH = 180; // Standard 180px height for 3x3 grid

  for (int i = 0; i < itemsPerPage && (pageStart + i) < files.size(); i++) {
    int index = pageStart + i;
    int col = i % columns;
    int row = i / columns;
    int x = horizontalPadding + col * (coverW + spacingX);
    int y = contentTop + row * (coverH + spacingY);
    bool selected = (selectorIndex == index);

    const std::string& name = files[index];
    bool isDir = name.back() == '/';

    if (isDir) {
      // Draw Directory Box
      renderer.drawRoundedRect(x, y, coverW, coverH, 2, 8, true);
      if (selected) renderer.drawRoundedRect(x - 3, y - 3, coverW + 6, coverH + 6, 3, 10, true);
      
      const uint8_t* icon = BaseTheme::iconForName(UIIcon::Folder, 48);
      if (icon) renderer.drawIcon(icon, x + (coverW - 48) / 2, y + 40, 48, 48, Color::DarkGray);
      
      std::string dirName = name.substr(0, name.length() - 1);
      std::string truncName = renderer.truncatedText(SMALL_FONT_ID, dirName.c_str(), coverW - 10);
      int tw = renderer.getTextWidth(SMALL_FONT_ID, truncName.c_str());
      renderer.drawText(SMALL_FONT_ID, x + (coverW - tw) / 2, y + 110, truncName.c_str());
    } else {
      // Draw Book Cover
      if (selected) renderer.drawRoundedRect(x - 4, y - 4, coverW + 8, coverH + 8, 4, 10, true);
      
      std::string prefix = basepath;
      if (prefix.back() != '/') prefix += "/";
      std::string fullPath = prefix + name;
      
      // Determine storage directory deterministically
      std::string sDir = LibraryStore::getStorageDirForPath(fullPath);

      bool hasThumb = false;
      if (!sDir.empty()) {
        std::string thumbPath = "/.crosspoint/" + sDir + "/thumb_180.bmp";
        if (!Storage.exists(thumbPath.c_str())) {
          // Lazy Generate 180px Thumbnail - must load first to find cover
          if (StringUtils::checkFileExtension(fullPath, ".epub")) {
            Epub epub(fullPath, "/.crosspoint");
            if (epub.load(true, true)) {
              epub.generateThumbBmp(180);
            }
          } else if (StringUtils::checkFileExtension(fullPath, ".xtc") || StringUtils::checkFileExtension(fullPath, ".xtch")) {
            Xtc xtc(fullPath, "/.crosspoint");
            if (xtc.load()) {
              xtc.generateThumbBmp(180);
            }
          }
        }

        if (Storage.exists(thumbPath.c_str())) {
          FsFile file;
          if (Storage.openFileForRead("HOME", thumbPath, file)) {
            Bitmap bmp(file);
            if (bmp.parseHeaders() == BmpReaderError::Ok) {
              renderer.drawBitmap(bmp, x, y, coverW, coverH);
              hasThumb = true;
            }
            file.close();
          }
        }
      }

      if (!hasThumb) {
        renderer.drawRoundedRect(x, y, coverW, coverH, 1, 8, true);
        const uint8_t* icon = BaseTheme::iconForName(UIIcon::Book, 48);
        if (icon) renderer.drawIcon(icon, x + (coverW - 48) / 2, y + (coverH - 48) / 2, 48, 48, Color::LightGray);
      }
    }
  }

  // Draw Page Indicators
  int totalPages = (files.size() + itemsPerPage - 1) / itemsPerPage;
  if (totalPages > 1) {
    int indicatorsY = pageHeight - metrics.buttonHintsHeight - 15;
    int indWidth = 20;
    int indSpacing = 10;
    int totalIndW = totalPages * indWidth + (totalPages - 1) * indSpacing;
    int startX = (pageWidth - totalIndW) / 2;

    for (int p = 0; p < totalPages; p++) {
      int ix = startX + p * (indWidth + indSpacing);
      if (p == currentPage) {
        renderer.fillRect(ix, indicatorsY, indWidth, 4, Black);
      } else {
        renderer.fillRect(ix, indicatorsY + 1, indWidth, 2, LightGray);
      }
    }
  }
}

void MyLibraryActivity::renderContextMenu() const {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int mw = 300, mh = 180;
  const int mx = (sw - mw) / 2, my = (sh - mh) / 2;

  renderer.fillRoundedRect(mx, my, mw, mh, 12, Color::White);
  renderer.drawRoundedRect(mx, my, mw, mh, 3, 12, true);

  const char* opts[] = {
    (viewMode == ViewMode::Grid) ? "Switch to List View" : "Switch to Grid View",
    "Delete File / Folder",
    "Cancel"
  };

  for (int i = 0; i < 3; i++) {
    int ry = my + 20 + i * 50;
    if (menuSelectedIndex == i) {
      renderer.fillRoundedRect(mx + 10, ry - 5, mw - 20, 42, 8, Color::Black);
    }
    renderer.drawText(UI_12_FONT_ID, mx + 20, ry + 4, opts[i], menuSelectedIndex != i);
  }
}

void MyLibraryActivity::renderConfirmDelete() const {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int mw = 300, mh = 160;
  const int mx = (sw - mw) / 2, my = (sh - mh) / 2;

  renderer.fillRoundedRect(mx, my, mw, mh, 12, Color::White);
  renderer.drawRoundedRect(mx, my, mw, mh, 3, 12, true);
  renderer.drawText(UI_12_FONT_ID, mx + 20, my + 20, "Confirm deletion?");

  const char* opts[] = {"Delete", "Cancel"};
  for (int i = 0; i < 2; i++) {
    int ry = my + 70 + i * 50;
    if (menuSelectedIndex == i) {
      renderer.fillRoundedRect(mx + 10, ry - 5, mw - 20, 42, 8, Color::Black);
    }
    renderer.drawText(UI_12_FONT_ID, mx + 20, ry + 4, opts[i], menuSelectedIndex != i);
  }
}

size_t MyLibraryActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}