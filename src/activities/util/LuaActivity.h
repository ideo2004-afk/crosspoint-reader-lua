#pragma once

#include "../Activity.h"
#include <string>

class LuaActivity final : public Activity {
public:
    LuaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                const std::string& pluginName, std::function<void()> onGoBack);

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(Activity::RenderLock&&) override {}  // unused: draw() called directly from loop()

private:
    std::string pluginName;
    std::function<void()> onGoBack;
    bool scriptLoaded = false;
    bool inputReady   = false;   // true once all buttons released after launch

    // Override render task to idle — Lua draw() runs on main loop for input sync
    [[noreturn]] void renderTaskLoop() override;

    void showError(const char* msg);
};
