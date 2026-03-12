#include "ReadingStatsActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeService.h"

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();

  topBooks = READING_STATS.getTopBooks(100);
  bookCount = static_cast<int>(topBooks.size());

  // Trigger first update
  requestUpdate();
}

void ReadingStatsActivity::onExit() {
  Activity::onExit();
  UITheme::getInstance().reload();
  topBooks.clear();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    onExitCallback();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, bookCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, bookCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, bookCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, bookCount);
    requestUpdate();
  });
}

void ReadingStatsActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, 
                 tr(STR_READING_STATS), CROSSPOINT_VERSION);

  // Calculate and display today's and total reading time
  uint32_t todayVal = TIME_SERVICE.getTodayValue();
  uint32_t todaySecs = READING_STATS.dailyReadingSeconds[todayVal];
  uint32_t todayHours = todaySecs / 3600;
  uint32_t todayMins = (todaySecs % 3600) / 60;

  uint32_t totalSecs = READING_STATS.totalReadingSeconds;
  uint32_t totalHours = totalSecs / 3600;
  uint32_t totalMins = (totalSecs % 3600) / 60;

  char todayTimeStr[64];
  snprintf(todayTimeStr, sizeof(todayTimeStr), "%s: %luh %lum", tr(STR_TODAY_READING_TIME), 
           static_cast<unsigned long>(todayHours), static_cast<unsigned long>(todayMins));

  char totalTimeStr[64];
  snprintf(totalTimeStr, sizeof(totalTimeStr), "%s: %luh %lum", tr(STR_TOTAL_READING_TIME), 
           static_cast<unsigned long>(totalHours), static_cast<unsigned long>(totalMins));

  int headerBottom = metrics.topPadding + metrics.headerHeight + 10;
  renderer.drawText(UI_12_FONT_ID, 10, headerBottom, todayTimeStr, true, EpdFontFamily::BOLD);
  headerBottom += renderer.getLineHeight(UI_12_FONT_ID) + 2;
  renderer.drawText(UI_12_FONT_ID, 10, headerBottom, totalTimeStr, true, EpdFontFamily::BOLD);
  headerBottom += renderer.getLineHeight(UI_12_FONT_ID) + 5;

  // Display last 7 days daily stats
  std::vector<uint32_t> last7Days;
  uint32_t today = TIME_SERVICE.getTodayValue();
  if (today > 0) {
    // Generate last 7 days keys (roughly, for YYYYMMDD it's a bit tricky with month boundaries, 
    // but we can iterate backwards and use ctime if needed, or just iterate the map)
    int count = 0;
    for (auto it = READING_STATS.dailyReadingSeconds.rbegin(); 
         it != READING_STATS.dailyReadingSeconds.rend() && count < 7; ++it, ++count) {
      uint32_t day = it->first;
      uint32_t secs = it->second;
      
      char dayStr[32];
      uint32_t y = day / 10000;
      uint32_t m = (day / 100) % 100;
      uint32_t d = day % 100;
      
      uint32_t h = secs / 3600;
      uint32_t min = (secs % 3600) / 60;
      
      snprintf(dayStr, sizeof(dayStr), "%02u/%02u: %luh %lum", m, d, (unsigned long)h, (unsigned long)min);
      renderer.drawText(SMALL_FONT_ID, 20, headerBottom, dayStr);
      headerBottom += renderer.getLineHeight(SMALL_FONT_ID) + 2;
    }
  }

  int listStartY = headerBottom + 10;
  int listHeight = pageHeight - listStartY - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, listStartY, pageWidth, listHeight}, bookCount, selectedIndex,
      [this](int index) {
        std::string filename = topBooks[index].path;
        size_t lastSlash = filename.find_last_of('/');
        if (lastSlash != std::string::npos) {
          filename = filename.substr(lastSlash + 1);
        }
        size_t lastDot = filename.find_last_of('.');
        if (lastDot != std::string::npos) {
          filename = filename.substr(0, lastDot);
        }
        return filename;
      },
      [this](int index) {
        uint32_t secs = topBooks[index].readingSeconds;
        uint32_t h = secs / 3600;
        uint32_t m = (secs % 3600) / 60;
        char timeStr[32];
        if (h > 0) {
          snprintf(timeStr, sizeof(timeStr), "%luh %lum", static_cast<unsigned long>(h), static_cast<unsigned long>(m));
        } else {
          snprintf(timeStr, sizeof(timeStr), "%lum", static_cast<unsigned long>(m));
        }
        return std::string(timeStr);
      },
      nullptr, nullptr, false);

  renderer.displayBuffer();
}
