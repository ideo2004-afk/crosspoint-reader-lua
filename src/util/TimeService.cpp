#include "TimeService.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_newlib.h>
#include <esp_timer.h>
#include <esp_rtc_time.h>
#include <esp_sntp.h>

#include <ctime>
#include <sys/time.h>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 4000;
constexpr unsigned long WIFI_SYNC_CONNECT_BUDGET_MS = 8000;
constexpr uint8_t NTP_SYNC_RETRIES = 50;
constexpr unsigned long NTP_SYNC_DELAY_MS = 100;
constexpr int TOP_CLOCK_VERTICAL_PADDING = 6;
constexpr unsigned long INVALID_TIME_RETRY_MS = 5000;
constexpr const char* CLOCK_PLACEHOLDER = "--:--";
constexpr const char* DATE_PLACEHOLDER = "-- --- ----";
constexpr int TOP_INFO_BATTERY_RIGHT_PADDING = 12;
constexpr uint32_t RTC_TIME_STATE_MAGIC = 0x54494D45;  // "TIME"
constexpr char TIME_STATE_FILE_JSON[] = "/.crosspoint/time_state.json";
constexpr const char* MONTH_ABBREVIATIONS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

struct RtcTimeState {
  uint32_t magic;
  int64_t anchorEpoch;
  int64_t lastSuccessfulSyncEpoch;
  uint64_t rtcUsAtAnchor;
};

RTC_DATA_ATTR RtcTimeState rtcTimeState = {};

uint64_t getTimebaseUs() {
  const int64_t espTimerUs = esp_timer_get_time();
  if (espTimerUs > 0) {
    return static_cast<uint64_t>(espTimerUs);
  }

  return esp_rtc_get_time_us();
}
}  // namespace

TimeService& TimeService::getInstance() {
  static TimeService instance;
  return instance;
}

bool TimeService::isTimeValid(const time_t epoch) { return epoch >= VALID_TIME_THRESHOLD; }

time_t TimeService::getBestCurrentEpoch() const {
  const time_t systemNow = time(nullptr);
  time_t estimatedNow = 0;
  if (isTimeValid(anchorEpoch)) {
    if (rtcUsAtAnchor == 0) {
      estimatedNow = anchorEpoch;
    } else {
      const uint64_t currentRtcUs = getTimebaseUs();
      if (currentRtcUs < rtcUsAtAnchor) {
        // Some wake paths on this hardware appear to reset the RTC microsecond
        // counter, but we still prefer showing the last known wall clock instead of
        // placeholder dashes until WiFi has a chance to refresh it.
        estimatedNow = anchorEpoch;
      } else {
        estimatedNow = static_cast<time_t>(anchorEpoch + static_cast<int64_t>((currentRtcUs - rtcUsAtAnchor) / 1000000ULL));
      }
    }
  }

  if (isTimeValid(systemNow) && isTimeValid(estimatedNow)) {
    return systemNow >= estimatedNow ? systemNow : estimatedNow;
  }
  if (isTimeValid(systemNow)) {
    return systemNow;
  }
  if (isTimeValid(estimatedNow)) {
    return estimatedNow;
  }
  return 0;
}

bool TimeService::getBestLocalTime(struct tm* localTime) const {
  if (!localTime) {
    return false;
  }

  const time_t now = getBestCurrentEpoch();
  if (!isTimeValid(now)) {
    return false;
  }

  return localtime_r(&now, localTime) != nullptr;
}

bool TimeService::hasValidTime() const { return isTimeValid(getBestCurrentEpoch()); }

time_t TimeService::getCurrentEpoch() const { return getBestCurrentEpoch(); }

bool TimeService::setManualTime(const int year, const int month, const int day, const int hour, const int minute) {
  std::tm localTime = {};
  localTime.tm_year = year - 1900;
  localTime.tm_mon = month - 1;
  localTime.tm_mday = day;
  localTime.tm_hour = hour;
  localTime.tm_min = minute;
  localTime.tm_sec = 0;
  localTime.tm_isdst = -1;

  const time_t epoch = mktime(&localTime);
  if (!isTimeValid(epoch)) {
    return false;
  }

  timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  esp_sync_timekeeping_timers();
  adoptTimeSnapshot(epoch, getTimebaseUs(), 0);
  lastSuccessfulSyncEpoch = 0;
  lastSyncAttemptMs = 0;
  persistIfValid();
  return true;
}

void TimeService::adoptTimeSnapshot(const time_t newAnchorEpoch, const uint64_t newRtcUsAtAnchor,
                                    const time_t newLastSuccessfulSyncEpoch) {
  if (!isTimeValid(newAnchorEpoch) || newRtcUsAtAnchor == 0) {
    return;
  }

  anchorEpoch = newAnchorEpoch;
  rtcUsAtAnchor = newRtcUsAtAnchor;
  if (isTimeValid(newLastSuccessfulSyncEpoch)) {
    lastSuccessfulSyncEpoch = newLastSuccessfulSyncEpoch;
  } else if (!isTimeValid(lastSuccessfulSyncEpoch)) {
    lastSuccessfulSyncEpoch = newAnchorEpoch;
  }
}

void TimeService::begin() {
  setenv("TZ", "CST-8", 1);
  tzset();
  loadPersistedSnapshot();
  restoreRtcBackedTime();
  persistIfValid();
}

bool TimeService::shouldAttemptSync() const {
  if (lastSyncAttemptMs == 0) {
    return true;
  }

  const time_t now = getBestCurrentEpoch();
  const bool validTime = isTimeValid(now);
  const unsigned long retryBackoff = validTime ? RETRY_BACKOFF_MS : INVALID_TIME_RETRY_MS;
  if (millis() - lastSyncAttemptMs < retryBackoff) {
    return false;
  }

  if (!validTime) {
    return true;
  }

  if (!isTimeValid(lastSuccessfulSyncEpoch)) {
    return true;
  }

  return (now - lastSuccessfulSyncEpoch) >= SYNC_INTERVAL_SECONDS;
}

bool TimeService::connectWithSavedCredentials() const {
  const auto& credentials = WIFI_STORE.getCredentials();
  if (credentials.empty()) {
    LOG_DBG("TIME", "Skipping time sync: no saved WiFi credentials");
    return false;
  }

  const unsigned long connectBudgetStart = millis();

  auto tryConnect = [](const WifiCredential& cred) -> bool {
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);

    if (cred.password.empty()) {
      WiFi.begin(cred.ssid.c_str());
    } else {
      WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    }

    const unsigned long start = millis();
    while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      const wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        LOG_DBG("TIME", "Connected to WiFi for time sync: %s", cred.ssid.c_str());
        return true;
      }

      if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
        break;
      }

      delay(100);
    }

    WiFi.disconnect(false);
    delay(100);
    return false;
  };

  const std::string& lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    if (const auto* lastCred = WIFI_STORE.findCredential(lastSsid)) {
      if (tryConnect(*lastCred)) {
        return true;
      }
    }
  }

  for (const auto& cred : credentials) {
    if (millis() - connectBudgetStart >= WIFI_SYNC_CONNECT_BUDGET_MS) {
      break;
    }

    if (!lastSsid.empty() && cred.ssid == lastSsid) {
      continue;
    }

    if (tryConnect(cred)) {
      return true;
    }
  }

  LOG_DBG("TIME", "Time sync failed: could not connect to any saved WiFi network");
  return false;
}

void TimeService::disconnectWifi() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

bool TimeService::syncTimeOverNtp() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_setservername(2, "time.cloudflare.com");
  esp_sntp_init();

  for (uint8_t retry = 0; retry < NTP_SYNC_RETRIES; retry++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && isTimeValid(time(nullptr))) {
      esp_sync_timekeeping_timers();
      adoptTimeSnapshot(time(nullptr), getTimebaseUs(), time(nullptr));
      persistRtcBackedTime();
      LOG_DBG("TIME", "NTP time synced successfully");
      return true;
    }
    delay(NTP_SYNC_DELAY_MS);
  }

  LOG_DBG("TIME", "NTP sync timed out");
  return false;
}

void TimeService::loadPersistedSnapshot() {
  if (!Storage.exists(TIME_STATE_FILE_JSON)) {
    return;
  }

  const String json = Storage.readFile(TIME_STATE_FILE_JSON);
  if (json.isEmpty()) {
    return;
  }

  JsonDocument doc;
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("TIME", "Failed to parse persisted time state: %s", error.c_str());
    return;
  }

  const time_t persistedAnchorEpoch = doc["anchorEpoch"] | 0;
  const uint64_t persistedRtcUsAtAnchor = doc["rtcUsAtAnchor"] | 0ULL;
  const time_t persistedLastSuccessfulSyncEpoch = doc["lastSuccessfulSyncEpoch"] | 0;
  adoptTimeSnapshot(persistedAnchorEpoch, persistedRtcUsAtAnchor, persistedLastSuccessfulSyncEpoch);
}

void TimeService::savePersistedSnapshot() const {
  if (!isTimeValid(anchorEpoch) || rtcUsAtAnchor == 0) {
    return;
  }

  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["anchorEpoch"] = anchorEpoch;
  doc["rtcUsAtAnchor"] = rtcUsAtAnchor;
  doc["lastSuccessfulSyncEpoch"] = lastSuccessfulSyncEpoch;

  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(TIME_STATE_FILE_JSON, json)) {
    LOG_ERR("TIME", "Failed to save persisted time snapshot");
  }
}

void TimeService::restoreRtcBackedTime() {
  esp_set_time_from_rtc();

  const uint64_t currentRtcUs = getTimebaseUs();
  const time_t now = time(nullptr);

  if (rtcTimeState.magic == RTC_TIME_STATE_MAGIC &&
      rtcTimeState.rtcUsAtAnchor > 0 &&
      currentRtcUs >= rtcTimeState.rtcUsAtAnchor &&
      isTimeValid(static_cast<time_t>(rtcTimeState.anchorEpoch))) {
    const uint64_t elapsedUs = currentRtcUs - rtcTimeState.rtcUsAtAnchor;
    const time_t reconstructedEpoch =
        static_cast<time_t>(rtcTimeState.anchorEpoch + static_cast<int64_t>(elapsedUs / 1000000ULL));

    adoptTimeSnapshot(reconstructedEpoch, currentRtcUs, static_cast<time_t>(rtcTimeState.lastSuccessfulSyncEpoch));

    if (isTimeValid(reconstructedEpoch) &&
        (!isTimeValid(now) || llabs(static_cast<long long>(now - reconstructedEpoch)) > 2)) {
      timeval tv = {.tv_sec = reconstructedEpoch, .tv_usec = static_cast<suseconds_t>(elapsedUs % 1000000ULL)};
      settimeofday(&tv, nullptr);
    }

    LOG_DBG("TIME", "Restored time from RTC anchor (anchor=%lld, current_rtc_us=%llu, anchor_rtc_us=%llu)",
            static_cast<long long>(rtcTimeState.anchorEpoch), static_cast<unsigned long long>(currentRtcUs),
            static_cast<unsigned long long>(rtcTimeState.rtcUsAtAnchor));
    return;
  }

  if (isTimeValid(anchorEpoch) && rtcUsAtAnchor > 0 && currentRtcUs >= rtcUsAtAnchor) {
    const uint64_t elapsedUs = currentRtcUs - rtcUsAtAnchor;
    const time_t reconstructedEpoch =
        static_cast<time_t>(anchorEpoch + static_cast<int64_t>(elapsedUs / 1000000ULL));

    adoptTimeSnapshot(reconstructedEpoch, currentRtcUs, lastSuccessfulSyncEpoch);

    if (isTimeValid(reconstructedEpoch) &&
        (!isTimeValid(now) || llabs(static_cast<long long>(now - reconstructedEpoch)) > 2)) {
      timeval tv = {.tv_sec = reconstructedEpoch, .tv_usec = static_cast<suseconds_t>(elapsedUs % 1000000ULL)};
      settimeofday(&tv, nullptr);
    }

    LOG_DBG("TIME", "Restored time from persisted snapshot (anchor=%lld, current_rtc_us=%llu, anchor_rtc_us=%llu)",
            static_cast<long long>(anchorEpoch), static_cast<unsigned long long>(currentRtcUs),
            static_cast<unsigned long long>(rtcUsAtAnchor));
    return;
  }

  if (!isTimeValid(now)) {
    lastSuccessfulSyncEpoch = 0;
    LOG_DBG("TIME", "No valid RTC-backed time available at boot (magic=%lu, anchor=%lld, last_sync=%lld, rtc_us=%llu, anchor_rtc_us=%llu)",
            static_cast<unsigned long>(rtcTimeState.magic), static_cast<long long>(rtcTimeState.anchorEpoch),
            static_cast<long long>(rtcTimeState.lastSuccessfulSyncEpoch), static_cast<unsigned long long>(currentRtcUs),
            static_cast<unsigned long long>(rtcTimeState.rtcUsAtAnchor));
    return;
  }

  // After upgrading from older firmware, RTC may already hold a valid clock but
  // we won't have sync metadata yet. Adopt the recovered time so we don't force
  // an immediate WiFi retry on the first wake-up.
  adoptTimeSnapshot(now, currentRtcUs, now);
  persistRtcBackedTime();
  LOG_DBG("TIME", "Restored RTC-backed time without metadata; adopted current time as last sync");
}

void TimeService::persistRtcBackedTime() const {
  if (!isTimeValid(anchorEpoch) || rtcUsAtAnchor == 0 || !isTimeValid(lastSuccessfulSyncEpoch)) {
    return;
  }

  rtcTimeState.magic = RTC_TIME_STATE_MAGIC;
  rtcTimeState.anchorEpoch = static_cast<int64_t>(anchorEpoch);
  rtcTimeState.lastSuccessfulSyncEpoch = static_cast<int64_t>(lastSuccessfulSyncEpoch);
  rtcTimeState.rtcUsAtAnchor = rtcUsAtAnchor;
  LOG_DBG("TIME", "Persisted RTC anchor (now=%lld, last_sync=%lld, rtc_us=%llu)", static_cast<long long>(anchorEpoch),
          static_cast<long long>(lastSuccessfulSyncEpoch), static_cast<unsigned long long>(rtcTimeState.rtcUsAtAnchor));
}

void TimeService::persistIfValid() {
  const time_t now = getBestCurrentEpoch();
  if (!isTimeValid(now)) {
    return;
  }

  adoptTimeSnapshot(now, getTimebaseUs(), lastSuccessfulSyncEpoch);
  persistRtcBackedTime();
  savePersistedSnapshot();
}

bool TimeService::syncIfDue() {
  if (!SETTINGS.statusBarClock) {
    return false;
  }

  if (!shouldAttemptSync()) {
    return false;
  }

  lastSyncAttemptMs = millis();
  LOG_DBG("TIME", "Attempting time sync (valid=%d, wifi=%d)", hasValidTime(), WiFi.status());

  const bool alreadyConnected = WiFi.status() == WL_CONNECTED;
  bool connected = alreadyConnected;
  if (!connected) {
    connected = connectWithSavedCredentials();
  }

  bool synced = false;
  if (connected) {
    WiFi.setSleep(false);
    synced = syncTimeOverNtp();
  }

  if (!alreadyConnected) {
    disconnectWifi();
  } else if (esp_sntp_enabled()) {
    esp_sntp_stop();
    WiFi.setSleep(true);
  }

  if (!synced) {
    LOG_DBG("TIME", "Time sync attempt finished without a valid clock");
  }
  return synced;
}

bool TimeService::formatDate(char* buffer, size_t bufferSize) const {
  if (!buffer || bufferSize < 12) {
    return false;
  }

  struct tm localTime;
  if (!getBestLocalTime(&localTime)) {
    return false;
  }

  if (localTime.tm_mon < 0 || localTime.tm_mon >= 12) {
    return false;
  }

  snprintf(buffer, bufferSize, "%02d-%s-%04d", localTime.tm_mday, MONTH_ABBREVIATIONS[localTime.tm_mon],
           localTime.tm_year + 1900);
  return true;
}

bool TimeService::formatClock(char* buffer, size_t bufferSize) const {
  if (!buffer || bufferSize < 6) {
    return false;
  }

  struct tm localTime;
  if (!getBestLocalTime(&localTime)) {
    return false;
  }

  snprintf(buffer, bufferSize, "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  return true;
}

int TimeService::getTopInfoBarInset(const GfxRenderer& renderer) const {
  if (!SETTINGS.statusBarClock) {
    return 0;
  }

  const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
  const int batteryHeight = UITheme::getInstance().getMetrics().batteryHeight;
  return std::max(textHeight, batteryHeight) + (TOP_CLOCK_VERTICAL_PADDING * 2);
}

void TimeService::drawTopInfoBar(const GfxRenderer& renderer, const int topY, const bool showBattery,
                                 const char* rightTextOverride) const {
  if (!SETTINGS.statusBarClock) {
    return;
  }

  char dateStr[12] = {};
  char clockStr[6] = {};
  const char* dateText = dateStr;
  const char* timeText = clockStr;
  if (!formatDate(dateStr, sizeof(dateStr))) {
    dateText = DATE_PLACEHOLDER;
  }
  if (!formatClock(clockStr, sizeof(clockStr))) {
    timeText = CLOCK_PLACEHOLDER;
  }

  const int textY = topY + TOP_CLOCK_VERTICAL_PADDING;
  const int horizontalPadding = UITheme::getInstance().getMetrics().contentSidePadding;
  const int dateX = horizontalPadding;
  renderer.drawText(SMALL_FONT_ID, dateX, textY, dateText);

  if (showBattery) {
    renderer.drawCenteredText(SMALL_FONT_ID, textY, timeText);
    const auto& metrics = UITheme::getInstance().getMetrics();
    const bool showBatteryPercentage =
        SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
    const int screenWidth = renderer.getScreenWidth();
    const int batteryX = screenWidth - TOP_INFO_BATTERY_RIGHT_PADDING - metrics.batteryWidth;
    UITheme::getInstance().getTheme().drawBatteryRight(
        renderer, Rect{batteryX, textY, metrics.batteryWidth, metrics.batteryHeight}, showBatteryPercentage);
  } else if (rightTextOverride && rightTextOverride[0] != '\0') {
    renderer.drawCenteredText(SMALL_FONT_ID, textY, timeText);
    const int rightTextWidth = renderer.getTextWidth(SMALL_FONT_ID, rightTextOverride);
    const int rightTextX = renderer.getScreenWidth() - horizontalPadding - rightTextWidth;
    renderer.drawText(SMALL_FONT_ID, rightTextX, textY, rightTextOverride);
  } else {
    const int timeWidth = renderer.getTextWidth(SMALL_FONT_ID, timeText);
    const int timeX = renderer.getScreenWidth() - horizontalPadding - timeWidth;
    renderer.drawText(SMALL_FONT_ID, timeX, textY, timeText);
  }
}
uint32_t TimeService::getTodayValue() const {
  struct tm timeinfo;
  if (!getBestLocalTime(&timeinfo)) {
    return 0;
  }
  return (timeinfo.tm_year + 1900) * 10000 + (timeinfo.tm_mon + 1) * 100 + timeinfo.tm_mday;
}
