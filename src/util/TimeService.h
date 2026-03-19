#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

class GfxRenderer;

class TimeService {
 private:
  unsigned long lastSyncAttemptMs = 0;
  time_t lastSuccessfulSyncEpoch = 0;
  time_t anchorEpoch = 0;
  uint64_t rtcUsAtAnchor = 0;
  bool syncedThisBoot = false;

  static constexpr time_t VALID_TIME_THRESHOLD = 1;                // Allow any time >= epoch 1
  static constexpr time_t SYNC_INTERVAL_SECONDS = 12 * 60 * 60;   // 12 hours
  static constexpr unsigned long RETRY_BACKOFF_MS = 30UL * 60 * 1000;

  TimeService() = default;

  static bool isTimeValid(time_t epoch);
  bool shouldAttemptSync() const;
  bool connectWithSavedCredentials() const;
  static void disconnectWifi();
  bool syncTimeOverNtp();
  void syncTimeZoneFromIp();
  time_t getBestCurrentEpoch() const;
  bool getBestLocalTime(struct tm* localTime) const;
  void adoptTimeSnapshot(time_t newAnchorEpoch, uint64_t newRtcUsAtAnchor, time_t newLastSuccessfulSyncEpoch);
  void loadPersistedSnapshot();
  void savePersistedSnapshot() const;
  void restoreRtcBackedTime();
  void persistRtcBackedTime() const;

 public:
  TimeService(const TimeService&) = delete;
  TimeService& operator=(const TimeService&) = delete;

 static TimeService& getInstance();

  void begin();
  bool syncIfDue();
  bool syncNow();
  bool hasValidTime() const;
  time_t getCurrentEpoch() const;
  bool setManualTime(int year, int month, int day, int hour, int minute);
  uint32_t getTodayValue() const;
  void persistIfValid();
  bool formatDate(char* buffer, size_t bufferSize) const;
  bool formatClock(char* buffer, size_t bufferSize) const;
  int getTopInfoBarInset(const GfxRenderer& renderer) const;
  void drawTopInfoBar(const GfxRenderer& renderer, int topY, bool showBattery,
                     const char* rightTextOverride = nullptr) const;
};

#define TIME_SERVICE TimeService::getInstance()
