#include "FileTransferActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <qrcode.h>

#include "MappedInputManager.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// AP Mode configuration
constexpr const char* AP_SSID = "CrossPoint-Reader";
constexpr const char* AP_PASSWORD = nullptr;
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;
constexpr int QR_CODE_WIDTH = 6 * 33;
constexpr int QR_CODE_HEIGHT = 200;

DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;
}  // namespace

void FileTransferActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  LOG_DBG("FILETX", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  state = FileTransferState::STARTING;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  requestUpdate();

  if (isApMode) {
    state = FileTransferState::AP_STARTING;
    requestUpdate();
    startAccessPoint();
  } else {
    // STA mode - check if already connected
    if (WiFi.status() == WL_CONNECTED) {
      connectedIP = WiFi.localIP().toString().c_str();
      connectedSSID = WiFi.SSID().c_str();
      startWebServer();
    } else {
      WiFi.mode(WIFI_STA);
      state = FileTransferState::WIFI_SELECTION;
      LOG_DBG("FILETX", "Launching WifiSelectionActivity...");
      enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                                [this](const bool connected) { onWifiSelectionComplete(connected); }));
    }
  }
}

void FileTransferActivity::onExit() {
  ActivityWithSubactivity::onExit();

  LOG_DBG("FILETX", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());
  state = FileTransferState::SHUTTING_DOWN;

  stopWebServer();
  MDNS.end();

  if (dnsServer) {
    LOG_DBG("FILETX", "Stopping DNS server...");
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }

  delay(50);
  if (isApMode) {
    LOG_DBG("FILETX", "Stopping WiFi AP...");
    WiFi.softAPdisconnect(true);
  } else {
    LOG_DBG("FILETX", "Disconnecting WiFi (graceful)...");
    WiFi.disconnect(false);
  }
  delay(30);

  LOG_DBG("FILETX", "Setting WiFi mode OFF...");
  WiFi.mode(WIFI_OFF);
  delay(30);

  LOG_DBG("FILETX", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void FileTransferActivity::onWifiSelectionComplete(const bool connected) {
  LOG_DBG("FILETX", "WifiSelectionActivity completed, connected=%d", connected);

  if (connected) {
    if (subActivity) {
      connectedIP = static_cast<WifiSelectionActivity*>(subActivity.get())->getConnectedIP();
    } else {
      connectedIP = WiFi.localIP().toString().c_str();
    }
    connectedSSID = WiFi.SSID().c_str();

    exitActivity();

    if (MDNS.begin(AP_HOSTNAME)) {
      LOG_DBG("FILETX", "mDNS started: http://%s.local/", AP_HOSTNAME);
    }
    startWebServer();
  } else {
    onGoBack();
  }
}

void FileTransferActivity::startAccessPoint() {
  LOG_DBG("FILETX", "Starting Access Point mode...");
  WiFi.mode(WIFI_AP);
  delay(100);

  bool apStarted;
  if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8) {
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  } else {
    apStarted = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  }

  if (!apStarted) {
    LOG_ERR("FILETX", "ERROR: Failed to start Access Point!");
    onGoBack();
    return;
  }

  delay(100);

  const IPAddress apIP = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  connectedIP = ipStr;
  connectedSSID = AP_SSID;

  if (MDNS.begin(AP_HOSTNAME)) {
    LOG_DBG("FILETX", "mDNS started: http://%s.local/", AP_HOSTNAME);
  }

  dnsServer = new DNSServer();
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", apIP);
  LOG_DBG("FILETX", "DNS server started for captive portal");

  startWebServer();
}

void FileTransferActivity::startWebServer() {
  LOG_DBG("FILETX", "Starting web server...");
  webServer.reset(new CrossPointWebServer());
  webServer->begin();

  if (webServer->isRunning()) {
    state = FileTransferState::SERVER_RUNNING;
    LOG_DBG("FILETX", "Web server started successfully");

    {
      RenderLock lock(*this);
      render(std::move(lock));
    }
    LOG_DBG("FILETX", "Rendered File Transfer screen");
  } else {
    LOG_ERR("FILETX", "ERROR: Failed to start web server!");
    webServer.reset();
    onGoBack();
  }
}

void FileTransferActivity::stopWebServer() {
  if (webServer && webServer->isRunning()) {
    LOG_DBG("FILETX", "Stopping web server...");
    webServer->stop();
    LOG_DBG("FILETX", "Web server stopped");
  }
  webServer.reset();
}

void FileTransferActivity::loop() {
  ActivityWithSubactivity::loop();
  if (subActivity) return;

  if (state == FileTransferState::SERVER_RUNNING) {
    if (isApMode && dnsServer) {
      dnsServer->processNextRequest();
    }

    if (!isApMode && webServer && webServer->isRunning()) {
      static unsigned long lastWifiCheck = 0;
      if (millis() - lastWifiCheck > 2000) {
        lastWifiCheck = millis();
        const wl_status_t wifiStatus = WiFi.status();
        if (wifiStatus != WL_CONNECTED) {
          LOG_DBG("FILETX", "WiFi disconnected! Status: %d", wifiStatus);
          state = FileTransferState::SHUTTING_DOWN;
          requestUpdate();
          return;
        }
      }
    }

    if (webServer && webServer->isRunning()) {
      esp_task_wdt_reset();
      constexpr int MAX_ITERATIONS = 500;
      for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
        webServer->handleClient();
        if ((i & 0x1F) == 0x1F) {
          esp_task_wdt_reset();
        }
        if ((i & 0x3F) == 0x3F) {
          yield();
          mappedInput.update();
          if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            onGoBack();
            return;
          }
        }
      }
      lastHandleClientTime = millis();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onGoBack();
      return;
    }
  }
}

static void drawQRCode(const GfxRenderer& renderer, const int x, const int y, const std::string& data) {
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(4)];
  LOG_DBG("FILETX", "QR Code (%zu): %s", data.length(), data.c_str());

  qrcode_initText(&qrcode, qrcodeBytes, 4, ECC_LOW, data.c_str());
  const uint8_t px = 6;
  for (uint8_t cy = 0; cy < qrcode.size; cy++) {
    for (uint8_t cx = 0; cx < qrcode.size; cx++) {
      if (qrcode_getModule(&qrcode, cx, cy)) {
        renderer.fillRect(x + px * cx, y + px * cy, px, px, true);
      }
    }
  }
}

void FileTransferActivity::render(Activity::RenderLock&&) {
  if (state == FileTransferState::SERVER_RUNNING || state == FileTransferState::AP_STARTING) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER), nullptr);

    if (state == FileTransferState::SERVER_RUNNING) {
      GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                        connectedSSID.c_str());
      renderServerRunning();
    } else {
      const auto height = renderer.getLineHeight(UI_12_FONT_ID);
      const auto top = (pageHeight - height) / 2;
      renderer.drawCenteredText(UI_12_FONT_ID, top, tr(STR_STARTING_HOTSPOT));
    }
    
    const auto labels = mappedInput.mapLabels(BaseTheme::HINT_BACK, "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    
    renderer.displayBuffer();
  }
}

void FileTransferActivity::renderServerRunning() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  int startY = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;
  int height10 = renderer.getLineHeight(UI_12_FONT_ID);

  if (isApMode) {
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, startY, tr(STR_CONNECT_WIFI_HINT), true, EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    const std::string wifiConfig = std::string("WIFI:S:") + connectedSSID + ";;";
    drawQRCode(renderer, metrics.contentSidePadding, startY, wifiConfig);

    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80, connectedSSID.c_str());

    startY += QR_CODE_HEIGHT + 2 * metrics.verticalSpacing;

    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";
    std::string ipUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + connectedIP + "/";

    drawQRCode(renderer, metrics.contentSidePadding, startY, hostnameUrl);

    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80, hostnameUrl.c_str());
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 110, ipUrl.c_str());
  } else {
    startY += metrics.verticalSpacing * 2;

    renderer.drawCenteredText(UI_12_FONT_ID, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
    startY += height10;
    renderer.drawCenteredText(UI_12_FONT_ID, startY, tr(STR_SCAN_QR_HINT), true, EpdFontFamily::BOLD);
    startY += height10 + metrics.verticalSpacing * 2;

    std::string webInfo = "http://" + connectedIP + "/";
    drawQRCode(renderer, (pageWidth - QR_CODE_WIDTH) / 2, startY, webInfo);
    startY += QR_CODE_HEIGHT + metrics.verticalSpacing * 2;

    renderer.drawCenteredText(UI_12_FONT_ID, startY, webInfo.c_str(), true);
    startY += renderer.getLineHeight(UI_12_FONT_ID) + 15;

    std::string hostnameUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + AP_HOSTNAME + ".local/";
    renderer.drawCenteredText(SMALL_FONT_ID, startY, hostnameUrl.c_str(), true);
  }
}
