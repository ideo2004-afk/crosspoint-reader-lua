#pragma once

#include <string>
#include <vector>
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "CrossPointSettings.h"

class ThemeSwitcher {
 public:
  ThemeSwitcher();
  
  void show();
  void hide();
  bool isVisible() const { return visible; }
  
  // Returns true if a theme change was confirmed and should trigger a UI refresh
  bool handleInput(const MappedInputManager& input);
  
  void render(GfxRenderer& renderer) const;

 private:
  bool visible = false;
  int selectedIndex = 0; // 0: Flow, 1: Cover, 2: Time
  
  struct ThemeOption {
    const char* name;
    CrossPointSettings::UI_THEME themeType;
    const uint8_t* icon;
  };
  
  std::vector<ThemeOption> options;
};
