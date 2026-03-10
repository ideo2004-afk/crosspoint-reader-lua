#include "LuaActivity.h"
#include "util/LuaManager.h"
#include <HalDisplay.h>
#include "fontIds.h"

LuaActivity::LuaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                         const std::string& pluginName, std::function<void()> onGoBack)
    : Activity("Lua Plugin", renderer, mappedInput), pluginName(pluginName), onGoBack(onGoBack) {}

// Render task is kept alive but idle.
// Lua draw() is called directly from loop() so input and drawing share the same task.
[[noreturn]] void LuaActivity::renderTaskLoop() {
    while (true) { vTaskDelay(portMAX_DELAY); }
}

void LuaActivity::onEnter() {
    Activity::onEnter();  // Creates the (idle) render task + logging

    renderer.clearScreen();
    renderer.drawText(UI_12_FONT_ID, 20, 20, ("Loading " + pluginName + "...").c_str());
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);

    scriptLoaded = false;
    inputReady   = false;
    LuaManager::getInstance().begin(&renderer, &mappedInput);
}

void LuaActivity::onExit() {
    Activity::onExit();  // Stops render task before Lua state is closed
    LuaManager::getInstance().end();  // Closes Lua VM and frees all Lua memory
}

void LuaActivity::loop() {
    // Plugin requested exit via sys.exit()
    if (LuaManager::getInstance().checkAndClearWantsExit()) {
        onGoBack();
        return;
    }

    // Load script once
    if (!scriptLoaded) {
        LOG_INF("LUA", "Loading plugin '%s'", pluginName.c_str());
        if (!LuaManager::getInstance().runPlugin(pluginName)) {
            showError("Failed to load script");
            scriptLoaded = true;
            return;
        }
        scriptLoaded = true;
        LOG_INF("LUA", "Script loaded, calling init()");
        LuaManager::getInstance().callFunction("init");
    }

    // Wait for all buttons to be released before handing control to Lua.
    // This prevents the launch-button press from leaking into the first draw() call.
    if (!inputReady) {
        if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) {
            inputReady = true;
        }
        return;
    }

    // Call Lua draw() every loop — Lua decides when to actually refresh the display
    LuaManager::getInstance().callFunction("draw");
}

void LuaActivity::showError(const char* msg) {
    renderer.clearScreen();
    renderer.drawText(UI_12_FONT_ID, 20, 100, "LUA ERROR:", true, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, 20, 140, msg);
    renderer.drawText(UI_10_FONT_ID, 20, 300, "Press BACK to return");
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
