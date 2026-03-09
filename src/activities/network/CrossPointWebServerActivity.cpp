#include "CrossPointWebServerActivity.h"
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include "activities/util/LuaActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void CrossPointWebServerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  LOG_INF("WEBACT", "Launching NetworkModeSelectionActivity...");
  enterNewActivity(new NetworkModeSelectionActivity(
      renderer, mappedInput, [this](const NetworkMode mode) { onNetworkModeSelected(mode); },
      [this](const std::string& name) { 
          enterNewActivity(new LuaActivity(renderer, mappedInput, name, [this] {
              exitActivity();
              requestUpdate();
          }));
      },
      [this]() { onGoBack(); }
      ));
}

void CrossPointWebServerActivity::onExit() { ActivityWithSubactivity::onExit(); }
void CrossPointWebServerActivity::loop() { ActivityWithSubactivity::loop(); if (mappedInput.wasPressed(MappedInputManager::Button::Back)) onGoBack(); }
void CrossPointWebServerActivity::render(Activity::RenderLock&&) { renderer.clearScreen(); renderer.displayBuffer(); }

// Dummy implementations to satisfy headers (logic moved to Lua or simplified)
void CrossPointWebServerActivity::onNetworkModeSelected(NetworkMode mode) { (void)mode; }
void CrossPointWebServerActivity::onWifiSelectionComplete(bool connected) { (void)connected; }
void CrossPointWebServerActivity::startAccessPoint() {}
void CrossPointWebServerActivity::startWebServer() {}
void CrossPointWebServerActivity::stopWebServer() {}
void CrossPointWebServerActivity::renderServerRunning() const {}
