#include "FlowTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstdint>
#include <string>

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"
#include "CrossPointSettings.h"
#include "util/TimeService.h"

namespace {
constexpr int cornerRadius = 6;
constexpr int sideCoverWidth = 66; // 30% of 220
constexpr int centerCoverWidth = 220;
constexpr int centerCoverHeight = 320;
constexpr int sideInnerHeight = 288; // 90% of 320
constexpr int sideOuterHeight = 256; // 80% of 320
constexpr int sideFarOuterHeight = 288; 
constexpr int hPadding = 10;
constexpr int bookCornerRadius = 6;

// Helper to "cut" corners of a rectangular area by erasing pixels outside the radius.
// This simulates rounded corners for bitmaps (which are always rectangular).
void cutRoundedCorners(GfxRenderer& renderer, int x, int y, int w, int h, int r) {
    const int rSq = r * r;
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            // Distance from center of corner arc (r, r)
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
// Helper to draw a single 7-segment digit
// x,y: top-left of the digit box
// w,h: width and height of the digit
// thickness: thickness of the segments
void draw7SegmentDigit(GfxRenderer& renderer, int x, int y, int w, int h, int digit, int thickness, Color color) {
    if (digit < 0 || digit > 9) return;
    
    // Segment mapping (standard a-g)
    //    -a-
    //   f   b
    //    -g-
    //   e   c
    //    -d-
    static const bool segments[10][7] = {
        {1, 1, 1, 1, 1, 1, 0}, // 0
        {0, 1, 1, 0, 0, 0, 0}, // 1
        {1, 1, 0, 1, 1, 0, 1}, // 2
        {1, 1, 1, 1, 0, 0, 1}, // 3
        {0, 1, 1, 0, 0, 1, 1}, // 4
        {1, 0, 1, 1, 0, 1, 1}, // 5
        {1, 0, 1, 1, 1, 1, 1}, // 6
        {1, 1, 1, 0, 0, 0, 0}, // 7
        {1, 1, 1, 1, 1, 1, 1}, // 8
        {1, 1, 1, 1, 0, 1, 1}  // 9
    };
    
    const bool* seg = segments[digit];
    const int midY = y + h / 2;
    
    // a (top)
    if (seg[0]) renderer.fillRect(x + thickness, y, w - 2 * thickness, thickness, color == Color::Black);
    // b (top-right)
    if (seg[1]) renderer.fillRect(x + w - thickness, y + thickness, thickness, midY - y - thickness - thickness/2, color == Color::Black);
    // c (bottom-right)
    if (seg[2]) renderer.fillRect(x + w - thickness, midY + thickness/2, thickness, y + h - midY - thickness - thickness/2, color == Color::Black);
    // d (bottom)
    if (seg[3]) renderer.fillRect(x + thickness, y + h - thickness, w - 2 * thickness, thickness, color == Color::Black);
    // e (bottom-left)
    if (seg[4]) renderer.fillRect(x, midY + thickness/2, thickness, y + h - midY - thickness - thickness/2, color == Color::Black);
    // f (top-left)
    if (seg[5]) renderer.fillRect(x, y + thickness, thickness, midY - y - thickness - thickness/2, color == Color::Black);
    // g (middle)
    if (seg[6]) renderer.fillRect(x + thickness, midY - thickness/2, w - 2 * thickness, thickness, color == Color::Black);
}

void draw7SegmentTime(GfxRenderer& renderer, int x, int y, int digitH, const char* timeStr, Color color, int thickness) {
    int digitW = digitH * 0.3; // Adjusted from 0.6 to keep width same while height doubles
    if (thickness <= 0) thickness = std::max(2, digitH / 10);
    int spacing = digitW / 4;
    int curX = x;
    
    for (int i = 0; timeStr[i] != '\0'; i++) {
        char c = timeStr[i];
        if (c >= '0' && c <= '9') {
            draw7SegmentDigit(renderer, curX, y, digitW, digitH, c - '0', thickness, color);
            curX += digitW + spacing;
        } else if (c == ':') {
            // Draw separator dots
            int dotSize = thickness;
            renderer.fillRect(curX + spacing/2, y + digitH/3 - dotSize/2, dotSize, dotSize, color == Color::Black);
            renderer.fillRect(curX + spacing/2, y + 2*digitH/3 - dotSize/2, dotSize, dotSize, color == Color::Black);
            curX += dotSize + spacing * 2;
        }
    }
}
}  // namespace

void FlowTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                   const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                   bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                   const char* btn1, const char* btn2, const char* btn3,
                                   const char* btn4, uint8_t highlightMask) const {
  const bool hasRecentBooks = !recentBooks.empty();
  const int pageWidth = renderer.getScreenWidth();

  // Draw Battery Icon for FlowTheme home screen
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = pageWidth - 12 - FlowMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, 5, FlowMetrics::values.batteryWidth, FlowMetrics::values.batteryHeight},
                   showBatteryPercentage);

  const int centerY = rect.y + 40; // Moved up slightly from 45 to 40 to ensure menu clearance
  const int centerX = pageWidth / 2;
  if (hasRecentBooks) {
    int count = recentBooks.size();
    // Use bookSelectorIndex logic if possible. 
    // If selectorIndex >= 1000, it means focus is elsewhere but we center on (selectorIndex - 1000)
    bool hasSelection = (selectorIndex >= 0 && selectorIndex < count);
    int curIdx = hasSelection ? selectorIndex : (selectorIndex >= 1000 ? (selectorIndex - 1000) : 0);
    if (curIdx >= count) curIdx = 0;

    if (bufferRestored) {
      coverRendered = true;
      coverBufferStored = true;
      return;
    }
    
    // Per user request: [ 2 1 3 ] order
    // We want to show up to 2 side covers
    // Stacked Cover Helper
    auto drawStackedCover = [&](int idx, bool isLeft, bool isFar) {
        int w = sideCoverWidth;
        int hL, hR;
        
        // All side covers (2, 3, 4, 5) now use the same slant ratio
        if (isLeft) {
            // Left side covers: right edge is inner/taller
            hR = sideOuterHeight;
            hL = sideInnerHeight;
        } else {
            // Right side covers: left edge is inner/taller
            hR = sideInnerHeight;
            hL = sideOuterHeight;
        }
        
        // Direct X coordinate mapping based on user request:
        // [3] x=30, [2] x=80, [4] x=330, [5] x=380
        int drawX;
        if (isLeft) {
            drawX = isFar ? 30 : 80;   // [3] or [2]
        } else {
            drawX = isFar ? 385 : 335; // [5] or [4]
        }
        int hMax = std::max(hL, hR);
        int drawY = centerY + (centerCoverHeight / 2) - (hMax / 2); 
        
        const std::string coverPath = UITheme::getCoverThumbPath(recentBooks[idx].coverBmpPath, centerCoverHeight);
        FsFile file;
        
        bool success = false;
        if (!coverPath.empty() && Storage.openFileForRead("HOME", coverPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
                renderer.drawPerspectiveBitmap(bitmap, drawX, drawY, w, hL, hR);
                success = true;
            }
            file.close();
        }

        if (!success) {
            // Draw a simple trapezoid fill if no image
            renderer.fillRect(drawX, drawY, w, hMax, false);
        }
    };

    // Index mapping for 5 covers: [3 2 1 4 5]
    int idx1 = curIdx;
    int idx2 = (curIdx + count - 1) % count;
    int idx3 = (curIdx + count - 2) % count;
    int idx4 = (curIdx + 1) % count;
    int idx5 = (curIdx + 2) % count;

    // Draw Order: [3], [5], [2], [4], [1] (Outside-in)
    if (count >= 5) drawStackedCover(idx3, true, true);  // 3
    if (count >= 4) drawStackedCover(idx5, false, true); // 5
    if (count >= 2) drawStackedCover(idx2, true, false); // 2
    if (count >= 3) drawStackedCover(idx4, false, false);// 4

    // Draw Center Cover (Current)
    {
        const std::string coverPath = UITheme::getCoverThumbPath(recentBooks[curIdx].coverBmpPath, centerCoverHeight);
        FsFile file;
        int drawX = centerX - centerCoverWidth / 2;
        int drawY = centerY;
        
        // Clear background for center cover to ensure it "covers" sides
        // Inversion disabled here so fillRect draws true white, not dark-mode-inverted black
        renderer.setInvertEnabled(false);
        renderer.fillRect(drawX, drawY, centerCoverWidth, centerCoverHeight, false);

        bool success = false;
        if (!coverPath.empty() && Storage.openFileForRead("HOME", coverPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
                renderer.drawBitmap(bitmap, drawX, drawY, centerCoverWidth, centerCoverHeight);
                success = true;
            }
            file.close();
        }
        renderer.setInvertEnabled(renderer.isDarkMode());
        
        if (success) {
            cutRoundedCorners(renderer, drawX, drawY, centerCoverWidth, centerCoverHeight, bookCornerRadius);
        }
        
        renderer.drawRoundedRect(drawX, drawY, centerCoverWidth, centerCoverHeight, 1, bookCornerRadius, true);
        if (!success) {
             renderer.fillRoundedRect(drawX, drawY + centerCoverHeight/3, centerCoverWidth, 2*centerCoverHeight/3, bookCornerRadius, false, false, true, true, Color::Black);
             renderer.drawIcon(CoverIcon, drawX + centerCoverWidth/2 - 16, drawY + centerCoverHeight/2 - 16, 32, 32);
        }

        if (hasSelection) {
            // Highlight border if selected (Book focus)
            renderer.drawRoundedRect(drawX - 2, drawY - 2, centerCoverWidth + 4, centerCoverHeight + 4, 4, bookCornerRadius + 2, true);
        }

        // Draw File Name
        std::string filename = recentBooks[curIdx].path;
        size_t lastSlash = filename.find_last_of('/');
        if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
        size_t lastDot = filename.find_last_of('.');
        if (lastDot != std::string::npos && lastDot > 0) filename = filename.substr(0, lastDot);
        
        auto truncatedTitle = renderer.truncatedText(BOOKERLY_14_FONT_ID, filename.c_str(), pageWidth - 40);
        int titleWidth = renderer.getTextWidth(BOOKERLY_14_FONT_ID, truncatedTitle.c_str());
        // Draw above covers (offset from rect.y, moved up to match cover move)
        int titleY = rect.y - 5; 
        renderer.drawText(BOOKERLY_14_FONT_ID, centerX - titleWidth / 2, titleY, truncatedTitle.c_str(), true);

        // Draw reading time for THIS book below the title
        // books map is keyed by filename (basename), not full path
        uint32_t bookSeconds = 0;
        const std::string& bookPath = recentBooks[curIdx].path;
        const size_t bookSlash = bookPath.find_last_of('/');
        const std::string bookFilename = (bookSlash != std::string::npos) ? bookPath.substr(bookSlash + 1) : bookPath;
        auto it = READING_STATS.books.find(bookFilename);
        if (it != READING_STATS.books.end()) {
            bookSeconds = it->second.readingSeconds;
        }
        
        uint32_t hours = bookSeconds / 3600;
        uint32_t minutes = (bookSeconds % 3600) / 60;
        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "%uh %um", hours, minutes);
        
        int timeWidth = renderer.getTextWidth(SMALL_FONT_ID, timeStr);
        // Draw below the center cover: centerY + centerCoverHeight + 8
        renderer.drawText(SMALL_FONT_ID, centerX - timeWidth / 2, centerY + centerCoverHeight + 8, timeStr, Color::Black);
    }
    
    coverRendered = true;
    coverBufferStored = true; 

  } else {
    drawEmptyRecents(renderer, rect);
  }
  
    // (drawFooter removed to move reading time per-book)

    // Add button hints for Home navigation in Flow theme (Back button is inactive on Home)
    // Filter out "BACK" hint dynamically based on content (logic-aware)
    const char* h1 = (btn1 && strcmp(btn1, BaseTheme::HINT_BACK) == 0) ? nullptr : btn1;
    const char* h2 = (btn2 && strcmp(btn2, BaseTheme::HINT_BACK) == 0) ? nullptr : btn2;
    const char* h3 = (btn3 && strcmp(btn3, BaseTheme::HINT_BACK) == 0) ? nullptr : btn3;
    const char* h4 = (btn4 && strcmp(btn4, BaseTheme::HINT_BACK) == 0) ? nullptr : btn4;

    drawButtonHints(renderer, h1, h2, h3, h4, highlightMask);

    // Draw today's total reading time in the bottom-right corner (Casio style)
    {
        uint32_t todaySeconds = READING_STATS.getTodaySeconds();
        uint32_t hours = todaySeconds / 3600;
        uint32_t minutes = (todaySeconds % 3600) / 60;
        char todayTimeStr[32];
        snprintf(todayTimeStr, sizeof(todayTimeStr), "%02u:%02u", hours, minutes);

        int digitH = 96; // Doubled from 48
        int digitW = digitH * 0.3; // Half of 0.6 to keep width same
        int thickness = 4;        // FIXED thin lines for Casio style
        int spacing = digitW / 4;
        int colonW = thickness + spacing * 2;
        int totalWidth = digitW * 4 + spacing * 3 + colonW; // space for 00:00

        // Position: Align with previous requested coordinates but adjusted for new size
        // The user previously wanted renderer.getScreenWidth() - todayTimeWidth - 32
        // Let's keep a similar right margin.
        int drawX = renderer.getScreenWidth() - totalWidth - 32;
        int drawY = renderer.getScreenHeight() - digitH - 100;

        draw7SegmentTime(renderer, drawX, drawY, digitH, todayTimeStr, Color::Black, thickness);

        // Draw Date above the reading time, aligned right
        if (SETTINGS.statusBarClock) {
            char dateStr[32] = {};
            const char* dateText = TIME_SERVICE.formatDate(dateStr, sizeof(dateStr)) ? dateStr : "";
            int dateWidth = renderer.getTextWidth(SMALL_FONT_ID, dateText);
            // x = screenWidth - rightMargin(32) - dateWidth
            renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - 32 - dateWidth, drawY - 25, dateText, Color::Black);
        }
    }
}


void FlowTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon) const {
  const int rowHeight = FlowMetrics::values.menuRowHeight;
  const int spacing = FlowMetrics::values.menuSpacing;
  
  const int centerX = rect.width / 2;
  const int menuLeft = centerX - 190;
  const int menuWidth = 209; // Reduced to 55% of 380 to avoid overlapping with clock
  
  for (int i = 0; i < buttonCount; ++i) {
    const bool selected = (selectedIndex == i);
    int y = rect.y + i * (rowHeight + spacing);
    
    if (selected) {
      renderer.fillRoundedRect(menuLeft, y, menuWidth, rowHeight, cornerRadius, Color::Black);
    }
    
    // Left-align icon with 12px padding from menuLeft (aligns with covers)
    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, 32);
      if (iconBitmap != nullptr) {
        // Center icon vertically in rowHeight
        renderer.drawIcon(iconBitmap, menuLeft + 12, y + (rowHeight - 32) / 2, 32, 32, selected ? White : Black);
      }
    }
    
    std::string label = buttonLabel(i);
    // Dynamic Nudge: labels with descenders (g, j, p, q, y) need more upward correction to look visually centered.
    // Labels without them look "too high" if we use the same correction.
    bool hasDescenders = label.find_first_of("gjpqy") != std::string::npos;
    int nudge = hasDescenders ? -8 : -4;
    
    // Text starts after the icon area (rowHeight ensures consistent spacing)
    int textY = y + (rowHeight - renderer.getLineHeight(NOTOSANS_12_FONT_ID)) / 2 + nudge;
    renderer.drawText(NOTOSANS_12_FONT_ID, menuLeft + rowHeight, textY, label.c_str(), selected ? White : Black, EpdFontFamily::REGULAR);
  }
}

void FlowTheme::drawFooter(GfxRenderer& renderer) const {
    // (Removed - moved to drawRecentBookCover for per-book stats)
}
