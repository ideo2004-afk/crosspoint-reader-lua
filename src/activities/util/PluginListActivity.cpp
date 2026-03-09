#include "PluginListActivity.h"
#include <HalStorage.h>
#include <GfxRenderer.h>
#include "components/UITheme.h"
#include "fontIds.h"
#include "activities/util/LuaActivity.h"
#include "I18n.h"

PluginListActivity::PluginListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, 
                                       std::function<void(const std::string& name)> onLaunchPlugin,
                                       std::function<void()> onGoBack)
    : Activity("Plugins", renderer, mappedInput), onGoBack(onGoBack), onLaunchPlugin(onLaunchPlugin) {}

void PluginListActivity::onEnter() {
    Activity::onEnter();
    luaPlugins.clear();
    
    LOG_INF("PLUGINS", "Scanning SD for plugins (aligned logic)...");
    
    auto root = Storage.open("/plugins");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        LOG_ERR("PLUGINS", "/plugins directory not found!");
        return;
    }

    root.rewindDirectory();

    char name[256];
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
        if (file.isDirectory()) {
            file.getName(name, sizeof(name));
            if (name[0] == '.') {
                file.close();
                continue;
            }

            std::string entryName = name;
            String mainPath = "/plugins/" + String(name) + "/main.lua";
            
            if (Storage.exists(mainPath.c_str())) {
                luaPlugins.push_back(entryName);
                LOG_INF("PLUGINS", "Found valid plugin: %s", name);
            }
        }
        file.close();
    }
    root.close();
    
    selectedIndex = 0;
    requestUpdate();
}

void PluginListActivity::onExit() { Activity::onExit(); }

void PluginListActivity::loop() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        onGoBack();
        return;
    }

    if (!luaPlugins.empty() && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        std::string name = luaPlugins[selectedIndex];
        onLaunchPlugin(name);
        return;
    }

    buttonNavigator.onNext([this] {
        if (!luaPlugins.empty()) {
            selectedIndex = (selectedIndex + 1) % luaPlugins.size();
            requestUpdate();
        }
    });

    buttonNavigator.onPrevious([this] {
        if (!luaPlugins.empty()) {
            selectedIndex = (selectedIndex == 0) ? luaPlugins.size() - 1 : selectedIndex - 1;
            requestUpdate();
        }
    });
}

void PluginListActivity::render(Activity::RenderLock&&) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Plugins");

    if (luaPlugins.empty()) {
        renderer.drawCenteredText(UI_12_FONT_ID, 300, "No Plugins Found");
        renderer.drawCenteredText(UI_10_FONT_ID, 350, "Check /plugins/ folder on SD");
    } else {
        auto rowTitle = [this](int index) { return luaPlugins[index].c_str(); };
        auto rowDesc = [](int index) { return std::string("Lua Script Extension"); };
        auto rowIcon = [](int index) { return UIIcon::Transfer; };

        const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
        const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight;

        GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(luaPlugins.size()),
                     selectedIndex, rowTitle, rowDesc, rowIcon);
    }

    const auto l = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OK_BUTTON), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, l.btn1, l.btn2, l.btn3, l.btn4);
    renderer.displayBuffer();
}
