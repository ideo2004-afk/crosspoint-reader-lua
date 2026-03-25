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

namespace {
void drawPanel(GfxRenderer& renderer, const Rect& rect) {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 6, true);
}

void drawSectionTitle(GfxRenderer& renderer, const Rect& rect, const char* title) {
  renderer.drawText(SMALL_FONT_ID, rect.x, rect.y, title, true, EpdFontFamily::BOLD);
  int titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title, EpdFontFamily::BOLD);
  int lineY = rect.y + renderer.getLineHeight(SMALL_FONT_ID) / 2;
  renderer.drawLine(rect.x + titleWidth + 10, lineY, rect.x + rect.width, lineY, true);
}

std::string formatMinutes(uint32_t seconds) {
  uint32_t mins = seconds / 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%u min", mins);
  return std::string(buf);
}
}

void ReadingStatsActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, 
                 tr(STR_READING_STATS), CROSSPOINT_VERSION);

  int contentTop = metrics.topPadding + metrics.headerHeight + 10;
  int sidePadding = 15;
  int contentWidth = pageWidth - sidePadding * 2;

  // 1. Today Panel
  Rect todayRect(sidePadding, contentTop, contentWidth, 80);
  drawPanel(renderer, todayRect);
  renderer.drawText(SMALL_FONT_ID, todayRect.x + 10, todayRect.y + 10, tr(STR_TODAY_READING_TIME), true, EpdFontFamily::BOLD);
  
  uint32_t todaySecs = READING_STATS.getTodaySeconds();
  std::string todayStr = formatMinutes(todaySecs);
  renderer.drawText(BOOKERLY_16_FONT_ID, todayRect.x + 15, todayRect.y + 35, todayStr.c_str(), true, EpdFontFamily::BOLD);

  // 2. Streak & Active Days (Small Panels)
  int summaryY = todayRect.y + todayRect.height + 10;
  int summaryWidth = (contentWidth - 10) / 2;
  Rect streakRect(sidePadding, summaryY, summaryWidth, 50);
  Rect activeRect(sidePadding + summaryWidth + 10, summaryY, summaryWidth, 50);

  drawPanel(renderer, streakRect);
  renderer.drawText(SMALL_FONT_ID, streakRect.x + 8, streakRect.y + 8, tr(STR_STATS_STREAK), true, EpdFontFamily::BOLD);
  char streakText[16];
  snprintf(streakText, sizeof(streakText), "%u days", READING_STATS.getCurrentStreakDays());
  renderer.drawText(UI_10_FONT_ID, streakRect.x + 10, streakRect.y + 30, streakText);

  drawPanel(renderer, activeRect);
  renderer.drawText(SMALL_FONT_ID, activeRect.x + 8, activeRect.y + 8, tr(STR_STATS_ACTIVE_DAYS), true, EpdFontFamily::BOLD);
  char activeText[16];
  snprintf(activeText, sizeof(activeText), "%u days", READING_STATS.getLifetimeActiveDays());
  renderer.drawText(UI_10_FONT_ID, activeRect.x + 10, activeRect.y + 30, activeText);

  // 3. Last 7 Days Chart
  int chartSectionY = summaryY + 50 + 15;
  drawSectionTitle(renderer, Rect(sidePadding, chartSectionY, contentWidth, 20), tr(STR_STATS_THIS_WEEK));
  
  int chartY = chartSectionY + 25;
  int chartHeight = 60;
  auto weeklyStats = READING_STATS.getRecentDays(7);
  uint32_t maxSecs = 1;
  for (const auto& day : weeklyStats) if (day.seconds > maxSecs) maxSecs = day.seconds;

  int barWidth = (contentWidth - 60) / 7;
  int startX = sidePadding + 30;
  
  for (int i = 0; i < 7; ++i) {
    int x = startX + i * (barWidth + 5);
    float ratio = (float)weeklyStats[i].seconds / maxSecs;
    int h = (int)(ratio * chartHeight);
    if (h < 2 && weeklyStats[i].seconds > 0) h = 2;
    
    // Draw bar
    if (h > 0) {
      renderer.fillRect(x, chartY + chartHeight - h, barWidth, h, true);
    } else {
      renderer.drawRect(x, chartY + chartHeight - 1, barWidth, 1, true);
    }

    // Days (MTWTFSS) - simplified
    const char* days[] = {"S", "M", "T", "W", "T", "F", "S"};
    struct tm t = {};
    t.tm_year = (weeklyStats[i].date / 10000) - 1900;
    t.tm_mon = ((weeklyStats[i].date / 100) % 100) - 1;
    t.tm_mday = (weeklyStats[i].date % 100);
    mktime(&t);
    renderer.drawText(SMALL_FONT_ID, x + barWidth/2 - 3, chartY + chartHeight + 5, days[t.tm_wday]);
  }

  // 4. Recent Books List
  int listSectionY = chartY + chartHeight + 25;
  drawSectionTitle(renderer, Rect(sidePadding, listSectionY, contentWidth, 20), tr(STR_STATS_RECENT_BOOKS));
  
  int listStartY = listSectionY + 25;
  int listHeight = pageHeight - listStartY - metrics.verticalSpacing - metrics.buttonHintsHeight;

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
        uint32_t lastDate = topBooks[index].lastReadDate;
        if (lastDate > 0) {
          uint32_t lm = (lastDate / 100) % 100;
          uint32_t ld = lastDate % 100;
          char lastBuf[32];
          snprintf(lastBuf, sizeof(lastBuf), " (L: %02u/%02u)", lm, ld);
          strcat(timeStr, lastBuf);
        }
        return std::string(timeStr);
      },
      nullptr, nullptr, false);

  const auto labels = mappedInput.mapLabels(BaseTheme::HINT_BACK, BaseTheme::HINT_OK, BaseTheme::HINT_PREV, BaseTheme::HINT_NEXT);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
