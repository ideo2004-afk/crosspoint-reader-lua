#include "NetworkModeSelectionActivity.h"
#include <GfxRenderer.h>
#include <I18n.h>
#include <HalStorage.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/wifi.h"
#include "components/icons/game.h"
#include "fontIds.h"
#include "activities/util/LuaActivity.h"

namespace {
const int BASE_MENU_COUNT = 3;
}  // namespace

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();
  luaPlugins.clear();
  LOG_INF("PLUGINS", "Scanning /plugins...");
  
  if (Storage.exists("/plugins")) {
      FsFile pDir = Storage.open("/plugins");
      if (pDir && pDir.isDirectory()) {
          FsFile entry;
          while (entry.openNext(&pDir, O_RDONLY)) {
              char name[64];
              entry.getName(name, sizeof(name));
              if (name[0] == '.') { entry.close(); continue; }
              String mainPath = "/plugins/" + String(name) + "/main.lua";
              if (Storage.exists(mainPath.c_str())) {
                  luaPlugins.push_back(name);
              }
              entry.close();
          }
          pDir.close();
      }
  }
  selectedIndex = 0;
  skipNextButtonCheck = true;
  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  if (skipNextButtonCheck) {
    if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) { skipNextButtonCheck = false; }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) { onCancel(); return; }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex < BASE_MENU_COUNT) {
        switch (selectedIndex) {
          case 0: onModeSelected(NetworkMode::JOIN_NETWORK); break;
          case 1: onModeSelected(NetworkMode::CONNECT_CALIBRE); break;
          case 2: onModeSelected(NetworkMode::CREATE_HOTSPOT); break;
        }
    } else {
        int luaIdx = selectedIndex - BASE_MENU_COUNT;
        onLaunchLua(luaPlugins[luaIdx]);
    }
    return;
  }

  int totalItems = BASE_MENU_COUNT + luaPlugins.size();
  buttonNavigator.onNext([this, totalItems] { selectedIndex = (selectedIndex + 1) % totalItems; requestUpdate(); });
  buttonNavigator.onPrevious([this, totalItems] { selectedIndex = (selectedIndex == 0) ? totalItems - 1 : selectedIndex - 1; requestUpdate(); });
}

void NetworkModeSelectionActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_HINT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  int totalItems = BASE_MENU_COUNT + luaPlugins.size();

  auto rowTitle = [this](int index) {
      if (index < BASE_MENU_COUNT) {
          static const char* titles[] = { tr(STR_JOIN_NETWORK), tr(STR_CALIBRE_WIRELESS), tr(STR_CREATE_HOTSPOT) };
          return titles[index];
      }
      return luaPlugins[index - BASE_MENU_COUNT].c_str();
  };

  auto rowDesc = [this](int index) {
      if (index < BASE_MENU_COUNT) {
          static const char* descs[] = { I18N.get(StrId::STR_JOIN_DESC), I18N.get(StrId::STR_CALIBRE_DESC), I18N.get(StrId::STR_HOTSPOT_DESC) };
          return std::string(descs[index]);
      }
      return std::string("Lua Plugin");
  };

  auto rowIcon = [this](int index) {
      if (index < BASE_MENU_COUNT) {
          switch (index) {
              case 0: return UIIcon::Wifi;
              case 1: return UIIcon::Library;
              case 2: return UIIcon::Hotspot;
              default: return UIIcon::Wifi;
          }
      }
      return UIIcon::Game;
  };

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectedIndex, rowTitle, rowDesc, rowIcon);
  const auto l = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, l.btn1, l.btn2, l.btn3, l.btn4);
  renderer.displayBuffer();
}
