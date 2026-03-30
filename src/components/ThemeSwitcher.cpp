#include "ThemeSwitcher.h"
#include "fontIds.h"
#include "components/icons/home_flow.h"
#include "components/icons/home_cover.h"
#include "components/icons/home_time.h"
#include "components/UITheme.h"
#include <cstring>

ThemeSwitcher::ThemeSwitcher() {
  options = {
      {"flow", CrossPointSettings::FLOW, HomeFlowIcon},
      {"cover", CrossPointSettings::COVER_THEME, HomeCoverIcon},
      {"time", CrossPointSettings::TIME_THEME, HomeTimeIcon}
  };
}

void ThemeSwitcher::show() {
  visible = true;
  // Initialize selection based on current theme
  auto currentTheme = SETTINGS.uiTheme;
  if (currentTheme == CrossPointSettings::FLOW) selectedIndex = 0;
  else if (currentTheme == CrossPointSettings::COVER_THEME) selectedIndex = 1;
  else if (currentTheme == CrossPointSettings::TIME_THEME) selectedIndex = 2;
  else selectedIndex = 0; // Default to flow
}

void ThemeSwitcher::hide() {
  visible = false;
}

bool ThemeSwitcher::handleInput(const MappedInputManager& input) {
  if (!visible) return false;

  if (input.wasReleased(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + static_cast<int>(options.size()) - 1) % options.size();
    return false; // Selection changed, redraw needed
  }
  
  if (input.wasReleased(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % options.size();
    return false; // Selection changed, redraw needed
  }
  
  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    auto newTheme = options[selectedIndex].themeType;
    if (SETTINGS.uiTheme != newTheme) {
      SETTINGS.uiTheme = newTheme;
      SETTINGS.saveToFile();
      UITheme::getInstance().reload();
      visible = false;
      return true; // Applied!
    }
    visible = false;
    return false;
  }
  
  if (input.wasReleased(MappedInputManager::Button::Back)) {
    visible = false;
    return false; // Cancelled
  }

  return false;
}

void ThemeSwitcher::render(GfxRenderer& renderer) const {
  if (!visible) return;

  const int pageWidth = renderer.getScreenWidth();
  const int switcherHeight = 100; // Maintain height for safety
  const int iconBaseY = 24;       // 1. Icon
  const int textY = 56;           // 2. Text
  const int underlineY = 96;      // 3. Underline 
  const int slotWidth = pageWidth / options.size();

  // 1. Draw Floating Window Background (Paper color: false)
  renderer.fillRect(0, 0, pageWidth, switcherHeight, false);
  renderer.drawLine(0, switcherHeight, pageWidth - 1, switcherHeight, 2, Color::Black);

  // 2. Draw Options
  for (size_t i = 0; i < options.size(); i++) {
    const auto& opt = options[i];
    const bool isSelected = (selectedIndex == (int)i);
    const int slotX = i * slotWidth;
    const int centerX = slotX + slotWidth / 2;

    // A. Draw Icon using Primitives (2px bold design)
    renderer.setInvertEnabled(renderer.isDarkMode());
    
    if (i == 0) { // FLOW: Center rect + side lines
        // Center Rect (2px stroke)
        renderer.drawRect(centerX - 6, iconBaseY, 12, 28, 2, true);
        // Left parallel lines (2px stroke)
        renderer.drawLine(centerX - 12, iconBaseY + 4, centerX - 12, iconBaseY + 24, 2, true);
        renderer.drawLine(centerX - 18, iconBaseY + 8, centerX - 18, iconBaseY + 20, 2, true);
        // Right parallel lines (2px stroke)
        renderer.drawLine(centerX + 12, iconBaseY + 4, centerX + 12, iconBaseY + 24, 2, true);
        renderer.drawLine(centerX + 18, iconBaseY + 8, centerX + 18, iconBaseY + 20, 2, true);
    } 
    else if (i == 1) { // COVER: 2x2 grid (2px stroke)
        int size = 11;
        int gap = 4;
        renderer.drawRect(centerX - size - gap/2, iconBaseY + 1, size, size, 2, true);
        renderer.drawRect(centerX + gap/2,        iconBaseY + 1, size, size, 2, true);
        renderer.drawRect(centerX - size - gap/2, iconBaseY + size + gap + 1, size, size, 2, true);
        renderer.drawRect(centerX + gap/2,        iconBaseY + size + gap + 1, size, size, 2, true);
    }
    else if (i == 2) { // TIME: Two top squares, one bottom rect (2px stroke)
        int size = 11;
        int gap = 4;
        renderer.drawRect(centerX - size - gap/2, iconBaseY + 1, size, size, 2, true);
        renderer.drawRect(centerX + gap/2,        iconBaseY + 1, size, size, 2, true);
        renderer.drawRect(centerX - size - gap/2, iconBaseY + size + gap + 1, size*2 + gap, size, 2, true);
    }
    
    renderer.setInvertEnabled(false);

    // B. Draw Label (Small font, Gray if unselected, Black if selected)
    TextColor textColor = isSelected ? Color::Black : Color::DarkGray;
    int tw = renderer.getTextWidth(SMALL_FONT_ID, opt.name, EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, centerX - tw / 2, textY, opt.name, textColor, EpdFontFamily::REGULAR);

    // C. Draw Selection Underline (Thick Ink bar, 48px wide centered)
    if (isSelected) {
      renderer.fillRect(centerX - 24, underlineY, 48, 4, true);
    }
  }
}
