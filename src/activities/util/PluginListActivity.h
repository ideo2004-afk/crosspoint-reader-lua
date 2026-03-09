#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"
#include <vector>
#include <string>

class PluginListActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::vector<std::string> luaPlugins;
  std::function<void()> onGoBack;
  std::function<void(const std::string& name)> onLaunchPlugin;

public:
  PluginListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, 
                     std::function<void(const std::string& name)> onLaunchPlugin,
                     std::function<void()> onGoBack);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
