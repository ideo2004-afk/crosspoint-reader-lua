#include "UITheme.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/flow/FlowTheme.h"
#include "components/themes/cover/CoverTheme.h"
#include "components/themes/time/TimeTheme.h"
#include "util/StringUtils.h"

namespace {
constexpr int SKIP_PAGE_MS = 700;
}  // namespace

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  switch (type) {
    case CrossPointSettings::UI_THEME::CLASSIC:
      LOG_DBG("UI", "Using Classic theme");
      currentTheme = std::make_unique<BaseTheme>();
      currentMetrics = &BaseMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::FLOW:
    default:
      LOG_DBG("UI", "Using Flow theme (default)");
      currentTheme = std::make_unique<FlowTheme>();
      currentMetrics = &FlowMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::COVER_THEME:
      LOG_DBG("UI", "Using Cover theme");
      currentTheme = std::make_unique<CoverTheme>();
      currentMetrics = &CoverMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::TIME_THEME:
      LOG_DBG("UI", "Using Time theme");
      currentTheme = std::make_unique<TimeTheme>();
      currentMetrics = &TimeMetrics::values;
      break;
  }
}

int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints) {
    reservedHeight += metrics.verticalSpacing + metrics.buttonHintsHeight;
  }
  const int availableHeight = renderer.getScreenHeight() - reservedHeight;
  int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  return availableHeight / rowHeight;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

UIIcon UITheme::getFileIcon(std::string filename) {
  if (filename.back() == '/') {
    std::string folderName = filename.substr(0, filename.length() - 1);
    if (folderName == "Books") return Book;
    if (folderName == "Games") return Game;
    if (folderName == "Pictures" || folderName == "Images") return Image;
    if (folderName == "Text" || folderName == "Documents") return Text;
    return Folder;
  }
  if (StringUtils::checkFileExtension(filename, ".epub") || StringUtils::checkFileExtension(filename, ".xtch") ||
      StringUtils::checkFileExtension(filename, ".xtc")) {
    return Book;
  }
  if (StringUtils::checkFileExtension(filename, ".txt") || StringUtils::checkFileExtension(filename, ".md")) {
    return Text;
  }
  if (StringUtils::checkFileExtension(filename, ".bmp")) {
    return Image;
  }
  return File;
}
