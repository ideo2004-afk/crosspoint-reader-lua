#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct BookStats {
  std::string path;
  std::string title;
  uint32_t readingSeconds = 0;
  uint32_t openCount = 0;
  uint32_t lastReadDate = 0; // YYYYMMDD
};

struct DailyStat {
  uint32_t date; // YYYYMMDD
  uint32_t seconds;
};

class ReadingStatsStore;
namespace JsonSettingsIO {
bool loadReadingStats(ReadingStatsStore& store, const char* json);
}

class ReadingStatsStore {
  static ReadingStatsStore instance;
  friend bool JsonSettingsIO::loadReadingStats(ReadingStatsStore&, const char*);

 public:
  uint32_t totalReadingSeconds = 0;
  std::map<std::string, BookStats> books;
  std::map<uint32_t, uint32_t> dailyReadingSeconds;

  static ReadingStatsStore& getInstance() { return instance; }

  void addReadingTime(const std::string& path, const std::string& title, uint32_t seconds);
  void recordOpen(const std::string& path, const std::string& title);
  void updatePath(const std::string& oldPath, const std::string& newPath);
  std::vector<BookStats> getTopBooks(size_t limit) const;

  bool saveToFile() const;
  bool loadFromFile();

 private:
  void pruneBooks();

 public:

  uint32_t getTodaySeconds() const;
  std::vector<DailyStat> getRecentDays(int limit) const;
  uint16_t getCurrentStreakDays() const;
  uint16_t getLifetimeActiveDays() const;
};

#define READING_STATS ReadingStatsStore::getInstance()
