#pragma once

#include <functional>
#include <vector>
#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Enum for network mode selection
enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT };

class NetworkModeSelectionActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  int selectedIndex = 0;
  bool skipNextButtonCheck = false;
  
  const std::function<void(const NetworkMode mode)> onModeSelected;
  const std::function<void()> onQubic;
  const std::function<void()> onGoToMiniGo;
  const std::function<void(const std::string& name)> onLaunchLua;
  const std::function<void()> onCancel;

  std::vector<std::string> luaPlugins;

 public:
  explicit NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        const std::function<void(NetworkMode)>& onModeSelected,
                                        const std::function<void()>& onQubic,
                                        const std::function<void()>& onGoToMiniGo,
                                        const std::function<void(const std::string& name)>& onLaunchLua,
                                        const std::function<void()>& onCancel)
      : Activity("NetworkModeSelection", renderer, mappedInput),
        onModeSelected(onModeSelected),
        onQubic(onQubic),
        onGoToMiniGo(onGoToMiniGo),
        onLaunchLua(onLaunchLua),
        onCancel(onCancel) {}
        
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
