#include "TimeTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstdint>
#include <string>

#include "RecentBooksStore.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "CrossPointSettings.h"
#include "components/icons/cover.h"

namespace {
constexpr int mainCoverHeight = 320;
constexpr int smallCoverHeight = 180;
constexpr int cornerRadius = 6;
constexpr int bookCornerRadius = 4;

void cutRoundedCorners(GfxRenderer& renderer, int x, int y, int w, int h, int r) {
    const int rSq = r * r;
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            const int distSq = (r - dx) * (r - dx) + (r - dy) * (r - dy);
            if (distSq > rSq) {
                renderer.drawPixel(x + dx, y + dy, false);                      // Top-left
                renderer.drawPixel(x + w - 1 - dx, y + dy, false);              // Top-right
                renderer.drawPixel(x + w - 1 - dx, y + h - 1 - dy, false);      // Bottom-right
                renderer.drawPixel(x + dx, y + h - 1 - dy, false);              // Bottom-left
            }
        }
    }
}

std::string getFilename(const std::string& path) {
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos) {
        return path.substr(lastSlash + 1);
    }
    return path;
}

std::string formatReadingTime(uint32_t seconds) {
    if (seconds == 0) return "00h:00m";
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    minutes %= 60;

    char buf[64];
    snprintf(buf, sizeof(buf), "%02uh:%02um", hours, minutes);
    return std::string(buf);
}

void drawBookCover(GfxRenderer& renderer, const RecentBook& book, int x, int y, int w, int h, bool selected, bool isMain) {
    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, h);
    FsFile file;
    
    // Clear background
    renderer.setInvertEnabled(false);
    renderer.fillRect(x, y, w, h, false);

    bool success = false;
    if (!coverPath.empty() && Storage.openFileForRead("HOME", coverPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            renderer.drawBitmap(bitmap, x, y, w, h);
            success = true;
        }
        file.close();
    }
    renderer.setInvertEnabled(renderer.isDarkMode());
    
    if (success) {
        cutRoundedCorners(renderer, x, y, w, h, bookCornerRadius);
        renderer.drawRoundedRect(x, y, w, h, 1, bookCornerRadius, true);
    } else {
        renderer.drawRoundedRect(x, y, w, h, 1, bookCornerRadius, true);
        renderer.fillRoundedRect(x + 1, y + 1, w - 2, h - 2, bookCornerRadius, Color::White);
        renderer.drawIcon(CoverIcon, x + (w - 32) / 2, y + (h - 32) / 2, 32, 32);
    }

    if (selected) {
        renderer.drawRoundedRect(x - 2, y - 2, w + 4, h + 4, 2, bookCornerRadius + 1, true);
    }
}
}  // namespace

void TimeTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                    const char* btn1, const char* btn2, const char* btn3,
                                    const char* btn4, uint8_t highlightMask) const {
  const bool hasRecentBooks = !recentBooks.empty();
  const int pageWidth = renderer.getScreenWidth();
  const int horizontalPadding = 25;

  const int mainW = 220;
  const int mainH = mainCoverHeight;
  const int mainX = 30; // Left padding
  const int mainY = rect.y + 20;

  const int infoX = mainX + mainW + 28; 
  int infoTopY = mainY + 30;

  // Draw Header (Battery & Date) - Every frame for TimeTheme
  // We use topPadding (5) as y to match sub-pages like Library/Settings.
  const auto& themeMetrics = UITheme::getInstance().getMetrics();
  drawHeader(renderer, Rect{0, themeMetrics.topPadding, pageWidth, rect.y}, nullptr, nullptr);

  if (hasRecentBooks) {
    const int count = recentBooks.size();
    int currentSelector = (selectorIndex >= 1000) ? (selectorIndex - 1000) : selectorIndex;
    bool hasSelection = (selectorIndex >= 0 && selectorIndex < 1000);

    if (bufferRestored) {
        coverRendered = true;
        coverBufferStored = true;
    } else {
        // --- Full Render Base Layout (WITHOUT selection) ---
        drawBookCover(renderer, recentBooks[0], mainX, mainY, mainW, mainH, false, true);

        // Metadata for Main Book
        int infoY = infoTopY;
        
        // Title for Main Book
        std::string title = recentBooks[0].title;
        renderer.drawText(UI_12_FONT_ID, infoX, infoY, renderer.truncatedText(UI_12_FONT_ID, title.c_str(), pageWidth - infoX - 20).c_str(), Black, EpdFontFamily::BOLD);
        infoY += 48; 
        
        // Author
        std::string author = recentBooks[0].author;
        if (author.empty()) author = "Unknown";
        renderer.drawText(UI_10_FONT_ID, infoX, infoY, renderer.truncatedText(UI_10_FONT_ID, author.c_str(), pageWidth - infoX - 20).c_str(), DarkGray);
        infoY += 40; 
        
        // Reading Time
        const std::string& bookPath = recentBooks[0].path;
        const size_t lastSlash = bookPath.find_last_of('/');
        const std::string bookFilename = (lastSlash != std::string::npos) ? bookPath.substr(lastSlash + 1) : bookPath;
        
        auto it = READING_STATS.books.find(bookFilename);
        uint32_t seconds = (it != READING_STATS.books.end()) ? it->second.readingSeconds : 0;
        std::string timeStr = formatReadingTime(seconds);
        renderer.drawText(SMALL_FONT_ID, infoX, infoY, timeStr.c_str(), DarkGray);
        
        // Progress Bar (Main Book)
        int progressY = infoY + 32;
        int progressW = pageWidth - infoX - 40;
        if (progressW > 240) progressW = 240;
        int progressH = 8;
        renderer.fillRectDither(infoX, progressY, progressW, progressH, Color::LightGray);
        int filledW = (recentBooks[0].progressPercent * progressW) / 100;
        if (filledW > 0) {
            renderer.fillRect(infoX, progressY, filledW, progressH, Black);
        }

        // Percentage Label
        char progBuf[16];
        snprintf(progBuf, sizeof(progBuf), "%02d%% Read", recentBooks[0].progressPercent);
        renderer.drawText(SMALL_FONT_ID, infoX, progressY + progressH + 8, progBuf, DarkGray);

        // --- 3. Reading Stats Chart ---
        int chartY = mainY + mainH + 42;
        int chartHeight = 84; 
        auto weeklyStats = READING_STATS.getRecentDays(7);
        uint32_t maxSecs = 1;
        for (const auto& day : weeklyStats) if (day.seconds > maxSecs) maxSecs = day.seconds;

        int chartLeft = horizontalPadding + 10;
        int chartRightPadding = horizontalPadding + 10;
        int chartAvailableWidth = pageWidth - chartLeft - chartRightPadding;
        int barSpacing = 6;
        int barWidth = (chartAvailableWidth - (barSpacing * 6)) / 7;
        
        for (int i = 0; i < 7; ++i) {
            int x = chartLeft + i * (barWidth + barSpacing);
            float ratio = (float)weeklyStats[i].seconds / maxSecs;
            int h = (int)(ratio * chartHeight);
            if (h < 2 && weeklyStats[i].seconds > 0) h = 2;
            
            // Draw bar
            if (h > 0) {
                renderer.fillRect(x, chartY + chartHeight - h, barWidth, h, Black);
            } else {
                renderer.drawRect(x, chartY + chartHeight - 1, barWidth, 1, Black);
            }

            // Days label (S M T W T F S)
            const char* days[] = {"S", "M", "T", "W", "T", "F", "S"};
            struct tm t = {};
            t.tm_year = (weeklyStats[i].date / 10000) - 1900;
            t.tm_mon = ((weeklyStats[i].date / 100) % 100) - 1;
            t.tm_mday = (weeklyStats[i].date % 100);
            mktime(&t);
            int tw = renderer.getTextWidth(SMALL_FONT_ID, days[t.tm_wday]);
            renderer.drawText(SMALL_FONT_ID, x + (barWidth - tw) / 2, chartY + chartHeight + 8, days[t.tm_wday]);
        }

        // --- 4. Statistics Tiles (moved up) ---
        const int tileSpacing = 12;
        const int tileW = (pageWidth - (horizontalPadding * 2) - (tileSpacing * 2)) / 3;
        const int tileH = 104; 
        const int tileY = chartY + chartHeight + 46; 

        uint32_t totalSecs = READING_STATS.totalReadingSeconds;
        uint32_t totalHours = totalSecs / 3600;
        char totalBuf[16];
        snprintf(totalBuf, sizeof(totalBuf), "%uh", totalHours);

        struct StatTile {
            std::string value;
            std::string label;
        };

        std::vector<StatTile> stats = {
            {std::to_string(READING_STATS.getTodaySeconds() / 60) + "m", "reading today"},
            {std::string(totalBuf), "reading time"},
            {std::to_string(READING_STATS.getLifetimeActiveDays()) + "d", "days of reading"}
        };

        for (int i = 0; i < 3; ++i) {
            int tx = horizontalPadding + i * (tileW + tileSpacing);
            // Draw background
            renderer.setInvertEnabled(false);
            renderer.fillRoundedRect(tx, tileY, tileW, tileH, 8, Color::LightGray);
            renderer.setInvertEnabled(renderer.isDarkMode());

            // Draw value (Large/Bold/Black)
            int tvw = renderer.getTextWidth(NOTOSANS_14_FONT_ID, stats[i].value.c_str());
            renderer.drawText(NOTOSANS_14_FONT_ID, tx + (tileW - tvw) / 2, tileY + 20, stats[i].value.c_str(), Black, EpdFontFamily::BOLD);

            // Draw label (Small/Black)
            int tlw = renderer.getTextWidth(SMALL_FONT_ID, stats[i].label.c_str());
            renderer.drawText(SMALL_FONT_ID, tx + (tileW - tlw) / 2, tileY + 60, stats[i].label.c_str(), Black);
        }

        // Store SnapShot after drawing books + chart + stats
        coverRendered = true;
        coverBufferStored = storeCoverBuffer();
    }

    // --- Draw Selection Border ON TOP (Every frame, outside buffer check) ---
    if (hasSelection && currentSelector == 0) {
        drawBookCover(renderer, recentBooks[0], mainX, mainY, mainW, mainH, true, true);
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void TimeTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) return;
  
  // Draw a 2px horizontal line at the top of the menu area
  renderer.drawLine(rect.x, rect.y, rect.x + rect.width, rect.y, 2, true);

  const int buttonWidth = rect.width / buttonCount;
  const int rowHeight = rect.height;
  
  // Make it more compact: reduce area used for the button focus box
  const int boxPaddingH = 15;
  const int boxPaddingV = 12;
  
  for (int i = 0; i < buttonCount; ++i) {
    const bool selected = (selectedIndex == i);
    int x = rect.x + i * buttonWidth;
    int y = rect.y;
    
    // Selection indicator: a 3px black line segment below the main horizontal line
    if (selected) {
      renderer.fillRect(x + 10, y + 2, buttonWidth - 20, 3, Black);
    }
    
    // Icon
    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, 32);
      if (iconBitmap != nullptr) {
        // Unselected icons are gray, selected are black
        renderer.drawIcon(iconBitmap, x + (buttonWidth - 32) / 2, y + boxPaddingV + 8, 32, 32, selected ? Black : DarkGray);
      }
    }
    
    // Label (English only as requested by user)
    const char* englishLabels[] = {"Library", "Recent", "Plugins", "Settings"};
    const char* label = (i < 4) ? englishLabels[i] : "";
    int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    int textY = y + boxPaddingV + 32 + 10;
    // Unselected text is gray, selected is black
    renderer.drawText(SMALL_FONT_ID, x + (buttonWidth - textWidth) / 2, textY, label, selected ? Black : DarkGray);
  }
}

void TimeTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  BaseTheme::drawHeader(renderer, rect, title, subtitle);
}
