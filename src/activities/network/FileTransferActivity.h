#pragma once

#include <functional>
#include <memory>
#include <string>

#include "activities/ActivityWithSubactivity.h"
#include "network/CrossPointWebServer.h"

enum class FileTransferState {
  STARTING,
  WIFI_SELECTION,
  AP_STARTING,
  SERVER_RUNNING,
  SHUTTING_DOWN
};

class FileTransferActivity final : public ActivityWithSubactivity {
  FileTransferState state = FileTransferState::STARTING;
  const std::function<void()> onGoBack;

  bool isApMode;

  std::unique_ptr<CrossPointWebServer> webServer;

  std::string connectedIP;
  std::string connectedSSID;

  unsigned long lastHandleClientTime = 0;

  void renderServerRunning() const;

  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startWebServer();
  void stopWebServer();

 public:
  explicit FileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void()>& onGoBack,
                                bool isApMode)
      : ActivityWithSubactivity("FileTransfer", renderer, mappedInput),
        onGoBack(onGoBack),
        isApMode(isApMode) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
