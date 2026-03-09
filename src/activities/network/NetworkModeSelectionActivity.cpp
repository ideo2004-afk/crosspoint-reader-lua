#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <HalStorage.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/game.h"
#include "components/icons/abba_go.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/wifi.h"
#include "fontIds.h"
#include "activities/util/LuaActivity.h"

namespace {
const int BASE_MENU_COUNT = 5;
}  // namespace

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  // Scan for Lua Plugins using official Storage interface
  luaPlugins.clear();
  LOG_INF("PLUGINS", "Scanning /plugins using HalStorage...");
  
  if (Storage.exists("/plugins")) {
      FsFile pDir = Storage.open("/plugins");
      if (pDir && pDir.isDirectory()) {
          FsFile entry;
          while (entry.openNext(&pDir, O_RDONLY)) {
              char name[64];
              entry.getName(name, sizeof(name));
              LOG_INF("PLUGINS", "Checking sub-item: %s", name);
              
              String mainPath = "/plugins/" + String(name) + "/main.lua";
              if (Storage.exists(mainPath.c_str())) {
                  luaPlugins.push_back(name);
                  LOG_INF("PLUGINS", "Validated plugin: %s", name);
              }
              entry.close();
          }
          pDir.close();
      }
  } else {
      LOG_ERR("PLUGINS", "/plugins directory not found via Storage!");
  }

  // Reset selection
  selectedIndex = 0;
  skipNextButtonCheck = true;
  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  if (skipNextButtonCheck) {
    if (!mappedInput.isAnyPressed() && !mappedInput.wasAnyReleased()) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // Handle back button - cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Handle confirm button - select current option
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex < BASE_MENU_COUNT) {
        switch (selectedIndex) {
          case 0: onModeSelected(NetworkMode::JOIN_NETWORK); break;
          case 1: onModeSelected(NetworkMode::CONNECT_CALIBRE); break;
          case 2: onModeSelected(NetworkMode::CREATE_HOTSPOT); break;
          case 3: onQubic(); break;
          case 4: onGoToMiniGo(); break;
        }
    } else {
        // Launch dynamic Lua Plugin via callback
        int luaIdx = selectedIndex - BASE_MENU_COUNT;
        std::string name = luaPlugins[luaIdx];
        onLaunchLua(name);
    }
    return;
  }

  int totalItems = BASE_MENU_COUNT + luaPlugins.size();

  // Handle navigation
  buttonNavigator.onNext([this, totalItems] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, totalItems] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });
}

void NetworkModeSelectionActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Toolbox");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  
  int totalItems = BASE_MENU_COUNT + luaPlugins.size();

  // Menu items and descriptions
  auto rowTitle = [this](int index) {
      if (index < BASE_MENU_COUNT) {
          static const char* titles[] = {
              tr(STR_JOIN_NETWORK), tr(STR_CALIBRE_WIRELESS), tr(STR_CREATE_HOTSPOT),
              "3D Tic-Tac-Toe", "ABBA Go"
          };
          return (const char*)titles[index];
      } else {
          return luaPlugins[index - BASE_MENU_COUNT].c_str();
      }
  };

  auto rowDesc = [this](int index) {
      if (index < BASE_MENU_COUNT) {
          static const char* descs[] = {
              I18N.get(StrId::STR_JOIN_DESC), I18N.get(StrId::STR_CALIBRE_DESC), I18N.get(StrId::STR_HOTSPOT_DESC),
              "3D board game", "Play Mini Go"
          };
          return std::string(descs[index]);
      } else {
          return std::string("External Lua Plugin");
      }
  };

  auto rowIcon = [this](int index) {
      if (index < BASE_MENU_COUNT) {
          switch (index) {
              case 0: return UIIcon::Wifi;
              case 1: return UIIcon::Library;
              case 2: return UIIcon::Hotspot;
              case 3: return UIIcon::Game;
              case 4: return UIIcon::AbbaGo;
              default: return UIIcon::Wifi;
          }
      } else {
          return UIIcon::Game; // Default icon for Lua plugins
      }
  };

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems,
               selectedIndex, rowTitle, rowDesc, rowIcon);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
