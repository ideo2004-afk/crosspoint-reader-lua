#include "CoverTheme.h"

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
constexpr int smallCoverHeight = 181;
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

void CoverTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                    const char* btn1, const char* btn2, const char* btn3,
                                    const char* btn4, uint8_t highlightMask) const {
  const bool hasRecentBooks = !recentBooks.empty();
  const int pageWidth = renderer.getScreenWidth();

  // Draw Header (Battery & Date) - Every frame for CoverTheme
  // We use topPadding (5) as y to match sub-pages like Library/Settings.
  const auto& themeMetrics = UITheme::getInstance().getMetrics();
  drawHeader(renderer, Rect{0, themeMetrics.topPadding, pageWidth, rect.y}, nullptr, nullptr);

  if (hasRecentBooks) {
    const int count = recentBooks.size();
    int currentSelector = (selectorIndex >= 1000) ? (selectorIndex - 1000) : selectorIndex;
    bool hasSelection = (selectorIndex >= 0 && selectorIndex < 1000);

    const int mainW = 220;
    const int mainH = mainCoverHeight;
    const int mainX = 30; // Left padding
    const int mainY = rect.y + 20;

    const int smallW = 124;
    const int smallH = smallCoverHeight;
    const int smallY = mainY + mainH + 80;
    const int horizontalPadding = 25;
    const int spacing = (pageWidth - (smallW * 3) - (horizontalPadding * 2)) / 2;

    if (bufferRestored) {
        coverRendered = true;
        coverBufferStored = true;
    } else if (!coverRendered) {
        // --- Full Render Base Layout (WITHOUT selection) ---
        drawBookCover(renderer, recentBooks[0], mainX, mainY, mainW, mainH, false, true);

        // Metadata for Main Book
        int infoX = mainX + mainW + 30;
        int infoY = mainY + 40;
        
        // Title for Main Book
        std::string title = recentBooks[0].title;
        renderer.drawText(UI_12_FONT_ID, infoX, infoY, renderer.truncatedText(UI_12_FONT_ID, title.c_str(), pageWidth - infoX - 20).c_str(), Black, EpdFontFamily::BOLD);
        infoY += 35;
        
        // Author
        std::string author = recentBooks[0].author;
        if (author.empty()) author = "Unknown";
        renderer.drawText(UI_10_FONT_ID, infoX, infoY, renderer.truncatedText(UI_10_FONT_ID, author.c_str(), pageWidth - infoX - 20).c_str(), DarkGray);
        infoY += 45;
        
        // Reading Time
        const std::string& bookPath = recentBooks[0].path;
        const size_t lastSlash = bookPath.find_last_of('/');
        const std::string bookFilename = (lastSlash != std::string::npos) ? bookPath.substr(lastSlash + 1) : bookPath;
        
        auto it = READING_STATS.books.find(bookFilename);
        uint32_t seconds = (it != READING_STATS.books.end()) ? it->second.readingSeconds : 0;
        std::string timeStr = formatReadingTime(seconds);
        renderer.drawText(SMALL_FONT_ID, infoX, infoY, timeStr.c_str(), DarkGray);
        
        // Progress Bar (Main Book)
        int progressY = infoY + 25;
        int progressW = 150;
        int progressH = 8;
        renderer.fillRectDither(infoX, progressY, progressW, progressH, Color::LightGray);
        int filledW = (recentBooks[0].progressPercent * progressW) / 100;
        if (filledW > 0) {
            renderer.fillRect(infoX, progressY, filledW, progressH, Black);
        }

        // Percentage Label (Hardcoded English only)
        char progBuf[16];
        snprintf(progBuf, sizeof(progBuf), "%02d%% Read", recentBooks[0].progressPercent);
        renderer.drawText(SMALL_FONT_ID, infoX, progressY + progressH + 5, progBuf, DarkGray);

        // Draw "Recent Books" label
        int labelY = mainY + mainH + 30;
        renderer.drawText(NOTOSANS_14_FONT_ID, 30, labelY, "Recent Books", Black, EpdFontFamily::BOLD);

        // 3 Small covers (WITHOUT selection)
        for (int i = 1; i < 4 && i < count; ++i) {
            int sx = horizontalPadding + (i - 1) * (smallW + spacing);
            drawBookCover(renderer, recentBooks[i], sx, smallY, smallW, smallH, false, false);
        }

        coverRendered = true;
        coverBufferStored = storeCoverBuffer();
    }

    // --- Draw Selection Border ON TOP (Every frame, outside buffer check) ---
    if (hasSelection) {
        if (currentSelector == 0) {
            drawBookCover(renderer, recentBooks[0], mainX, mainY, mainW, mainH, true, true);
        } else if (currentSelector < 4 && currentSelector < count) {
            int sx = horizontalPadding + (currentSelector - 1) * (smallW + spacing);
            drawBookCover(renderer, recentBooks[currentSelector], sx, smallY, smallW, smallH, true, false);
        }
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }


}

void CoverTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
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

void CoverTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  BaseTheme::drawHeader(renderer, rect, title, subtitle);
}
