#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Cover theme metrics
namespace CoverMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 40,
                                 .headerHeight = 84,
                                 .verticalSpacing = 16,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 45,
                                 .listWithSubtitleRowHeight = 75,
                                 .menuRowHeight = 56,
                                 .menuSpacing = 8,
                                 .tabSpacing = 12,
                                 .tabBarHeight = 40,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 320,      // Main cover height (Flow: 320)
                                 .homeCoverTileHeight = 600,  // Cover + Title area (adjusted for CoverTheme)
                                 .homeRecentBooksCount = 4,   // 1 main + 3 small
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

class CoverTheme : public BaseTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, const char* btn1 = nullptr,
                           const char* btn2 = nullptr, const char* btn3 = nullptr,
                           const char* btn4 = nullptr, uint8_t highlightMask = 0) const override;

  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;

  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const override;
};
