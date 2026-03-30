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

  const int centerY = rect.y + 40; // Moved up slightly from 45 to 40 to ensure menu clearance
  const int centerX = pageWidth / 2;

  if (hasRecentBooks) {
    int count = recentBooks.size();
    bool hasSelection = (selectorIndex >= 0 && selectorIndex < count);
    int curIdx = hasSelection ? selectorIndex : (selectorIndex >= 1000 ? (selectorIndex - 1000) : 0);
    if (curIdx >= count) curIdx = 0;

    if (bufferRestored) {
      coverRendered = true;
      coverBufferStored = true;
    } else if (!coverRendered) {
        // --- Full Render Base Layout (Snapshot Base) ---
        
        // 1. Draw Static Header Elements (Battery)
        const bool showBatteryPercentage = SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
        const int batteryX = pageWidth - 12 - FlowMetrics::values.batteryWidth;
        drawBatteryRight(renderer, Rect{batteryX, 16, FlowMetrics::values.batteryWidth, FlowMetrics::values.batteryHeight}, showBatteryPercentage);

        // 2. Draw Static Footer Hints
        drawButtonHints(renderer, btn1, btn2, btn3, btn4, highlightMask);

        // 3. Draw Top-Left Date (Consistent with other themes)
        if (SETTINGS.statusBarClock) {
            char dateStr[32] = {};
            const char* dateText = TIME_SERVICE.formatDate(dateStr, sizeof(dateStr)) ? dateStr : "";
            renderer.drawText(SMALL_FONT_ID, FlowMetrics::values.contentSidePadding, 16, dateText, Color::Black);
        }

        // Draw V-shape indicator (3px black line)
        int cx = renderer.getScreenWidth() / 2;
        int cy = 16; 
        renderer.drawLine(cx - 20, cy, cx, cy + 12, 3, Color::Black);
        renderer.drawLine(cx, cy + 12, cx + 20, cy, 3, Color::Black);

        // 4. Draw Static Clock (Casio Style for Today's Reading Time)
        {
            uint32_t todaySeconds = READING_STATS.getTodaySeconds();
            uint32_t hours = todaySeconds / 3600;
            uint32_t minutes = (todaySeconds % 3600) / 60;
            char todayTimeStr[32];
            snprintf(todayTimeStr, sizeof(todayTimeStr), "%02u:%02u", hours, minutes);

            int digitH = 96; 
            int digitW = digitH * 0.3; 
            int thickness = 4;
            int spacing = digitW / 4;
            int colonW = thickness + spacing * 2;
            int totalWidth = digitW * 4 + spacing * 3 + colonW;
            int drawX = renderer.getScreenWidth() - totalWidth - 32;
            int drawY = renderer.getScreenHeight() - digitH - 100;

            draw7SegmentTime(renderer, drawX, drawY, digitH, todayTimeStr, Color::Black, thickness);
        }

        // --- Finished Base drawing (Static Background ONLY), store snapshot ---
        coverRendered = true;
        coverBufferStored = storeCoverBuffer();
    }

    // --- Dynamic Elements (Redrawn Every Frame) ---
    // 1. Perspective Covers (Draw Order: [3], [5], [2], [4] for outside-in)
    auto drawStackedCover = [&](int idx, bool isLeft, bool isFar) {
        int w = sideCoverWidth;
        int hL, hR;
        if (isLeft) { hR = sideOuterHeight; hL = sideInnerHeight; } 
        else { hR = sideInnerHeight; hL = sideOuterHeight; }
        
        int drawX = isLeft ? (isFar ? 30 : 80) : (isFar ? 385 : 335);
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
        if (!success) renderer.fillRect(drawX, drawY, w, hMax, false);
    };

    int idx2 = (curIdx + count - 1) % count;
    int idx3 = (curIdx + count - 2) % count;
    int idx4 = (curIdx + 1) % count;
    int idx5 = (curIdx + 2) % count;

    if (count >= 5) drawStackedCover(idx3, true, true);  
    if (count >= 4) drawStackedCover(idx5, false, true); 
    if (count >= 2) drawStackedCover(idx2, true, false); 
    if (count >= 3) drawStackedCover(idx4, false, false);

    // 2. Center Cover (Main Focused Book)
    int cX = centerX - centerCoverWidth / 2;
    renderer.setInvertEnabled(false);
    renderer.fillRect(cX, centerY, centerCoverWidth, centerCoverHeight, false);

    const std::string cp = UITheme::getCoverThumbPath(recentBooks[curIdx].coverBmpPath, centerCoverHeight);
    FsFile cf;
    bool cs = false;
    if (!cp.empty() && Storage.openFileForRead("HOME", cp, cf)) {
        Bitmap bitmap(cf);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            renderer.drawBitmap(bitmap, cX, centerY, centerCoverWidth, centerCoverHeight);
            cs = true;
        }
        cf.close();
    }
    renderer.setInvertEnabled(renderer.isDarkMode());
    if (cs) cutRoundedCorners(renderer, cX, centerY, centerCoverWidth, centerCoverHeight, bookCornerRadius);
    renderer.drawRoundedRect(cX, centerY, centerCoverWidth, centerCoverHeight, 1, bookCornerRadius, true);
    if (!cs) {
          renderer.fillRoundedRect(cX, centerY + centerCoverHeight/3, centerCoverWidth, 2*centerCoverHeight/3, bookCornerRadius, false, false, true, true, Color::Black);
          renderer.drawIcon(CoverIcon, cX + centerCoverWidth/2 - 16, centerY + centerCoverHeight/2 - 16, 32, 32);
    }

    // 3. Selection Border
    if (hasSelection) {
        renderer.drawRoundedRect(cX - 2, centerY - 2, centerCoverWidth + 4, centerCoverHeight + 4, 4, bookCornerRadius + 2, true);
    }

    // 4. Metadata (Title + Reading Time)
    std::string filename = recentBooks[curIdx].path;
    size_t lastSlash = filename.find_last_of('/');
    if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos && lastDot > 0) filename = filename.substr(0, lastDot);
    
    auto truncatedTitle = renderer.truncatedText(BOOKERLY_14_FONT_ID, filename.c_str(), pageWidth - 40);
    int titleWidth = renderer.getTextWidth(BOOKERLY_14_FONT_ID, truncatedTitle.c_str());
    renderer.drawText(BOOKERLY_14_FONT_ID, centerX - titleWidth / 2, rect.y - 5, truncatedTitle.c_str(), true);

    uint32_t bookSeconds = 0;
    const std::string& bookPath = recentBooks[curIdx].path;
    const size_t bookSlash = bookPath.find_last_of('/');
    const std::string bBasename = (bookSlash != std::string::npos) ? bookPath.substr(bookSlash + 1) : bookPath;
    auto it = READING_STATS.books.find(bBasename);
    if (it != READING_STATS.books.end()) bookSeconds = it->second.readingSeconds;
    
    uint32_t hours = bookSeconds / 3600;
    uint32_t minutes = (bookSeconds % 3600) / 60;
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%uh %um", hours, minutes);
    int timeWidth = renderer.getTextWidth(SMALL_FONT_ID, timeStr);
    renderer.drawText(SMALL_FONT_ID, centerX - timeWidth / 2, centerY + centerCoverHeight + 8, timeStr, Color::Black);

  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void FlowTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  const int rowHeight = FlowMetrics::values.menuRowHeight;
  const int spacing = FlowMetrics::values.menuSpacing;
  
  const int centerX = rect.width / 2;
  const int menuLeft = centerX - 190;
  const int menuWidth = 209; 
  
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
