#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Flow theme metrics
namespace FlowMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 40,
                                 .headerHeight = 84,
                                 .verticalSpacing = 16,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 45,
                                 .listWithSubtitleRowHeight = 75,
                                 .menuRowHeight = 56,  // Increased for better readability
                                 .menuSpacing = 8,
                                 .tabSpacing = 12,
                                 .tabBarHeight = 40,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 320,      // 25-kai book ratio (~0.7)
                                 .homeCoverTileHeight = 380,  // Cover + Title area
                                 .homeRecentBooksCount = 7,   // Up to 7 books in carousel
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .bookProgressBarHeight = 4,
                                 .keyboardKeyWidth = 31,
                                 .keyboardKeyHeight = 50,
                                 .keyboardKeySpacing = 0,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = true};
}

class FlowTheme : public BaseTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, const char* btn1 = nullptr,
                           const char* btn2 = nullptr, const char* btn3 = nullptr,
                           const char* btn4 = nullptr) const override;

  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const override;
  void drawFooter(GfxRenderer& renderer) const;
};
