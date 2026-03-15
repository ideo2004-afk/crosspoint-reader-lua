#include "ReadingStatsStore.h"

#include <algorithm>
#include <HalStorage.h>

#include "JsonSettingsIO.h"
#include "util/TimeService.h"

void ReadingStatsStore::addReadingTime(const std::string& path, const std::string& title, uint32_t seconds) {
  if (seconds == 0) return;
  
  std::string filename = path;
  size_t lastSlash = filename.find_last_of('/');
  if (lastSlash != std::string::npos) {
    filename = filename.substr(lastSlash + 1);
  }

  auto& stat = books[filename];
  stat.path = path;
  if (!title.empty()) {
    stat.title = title;
  }
  stat.readingSeconds += seconds;
  totalReadingSeconds += seconds;

  uint32_t today = TIME_SERVICE.getTodayValue(); // YYYYMMDD
  if (today == 0) today = 19700101; // Fallback to 1970-01-01 if clock is completely invalid
  stat.lastReadDate = today;

  // Record daily stats
  dailyReadingSeconds[today] += seconds;
  
  // Cleanup: Keep only last 30 days (though we only display 7)
  if (dailyReadingSeconds.size() > 30) {
    auto it = dailyReadingSeconds.begin();
    // Don't erase the fallback entry if it's the only one or if it's active
    if (it->first != 19700101 || dailyReadingSeconds.size() > 31) {
       dailyReadingSeconds.erase(it);
    }
  }

  pruneBooks();
}

void ReadingStatsStore::recordOpen(const std::string& path, const std::string& title) {
  std::string filename = path;
  size_t lastSlash = filename.find_last_of('/');
  if (lastSlash != std::string::npos) {
    filename = filename.substr(lastSlash + 1);
  }

  auto& stat = books[filename];
  stat.path = path;
  if (!title.empty()) {
    stat.title = title;
  }
  stat.openCount++;
  stat.lastReadDate = TIME_SERVICE.getTodayValue();

  pruneBooks();
}

void ReadingStatsStore::updatePath(const std::string& oldPath, const std::string& newPath) {
  auto it = books.find(oldPath);
  if (it != books.end()) {
    BookStats stats = it->second;
    stats.path = newPath;
    books[newPath] = stats;
    books.erase(it);
  }
}

std::vector<BookStats> ReadingStatsStore::getTopBooks(size_t limit) const {
  std::vector<BookStats> allBooks;
  for (const auto& pair : books) {
    allBooks.push_back(pair.second);
  }

  // Sort descending by reading time
  std::sort(allBooks.begin(), allBooks.end(),
            [](const BookStats& a, const BookStats& b) { return a.readingSeconds > b.readingSeconds; });

  if (allBooks.size() > limit) {
    allBooks.resize(limit);
  }
  return allBooks;
}

// Static instance
ReadingStatsStore ReadingStatsStore::instance;

bool ReadingStatsStore::saveToFile() const {
  return JsonSettingsIO::saveReadingStats(*this, "/.crosspoint/ReadingStats.json");
}

bool ReadingStatsStore::loadFromFile() {
  String json = Storage.readFile("/.crosspoint/ReadingStats.json");
  if (json.isEmpty()) {
    return false;
  }
  return JsonSettingsIO::loadReadingStats(*this, json.c_str());
}
namespace {
uint32_t offsetDay(uint32_t date, int days) {
  struct tm t = {};
  t.tm_year = (date / 10000) - 1900;
  t.tm_mon = ((date / 100) % 100) - 1;
  t.tm_mday = (date % 100);
  t.tm_hour = 12;
  t.tm_isdst = -1;
  time_t epoch = mktime(&t);
  if (epoch == (time_t)-1) return 0;
  epoch += (time_t)days * 24 * 3600;
  struct tm t2;
  localtime_r(&epoch, &t2);
  return (t2.tm_year + 1900) * 10000 + (t2.tm_mon + 1) * 100 + t2.tm_mday;
}
}

uint32_t ReadingStatsStore::getTodaySeconds() const {
  uint32_t today = TIME_SERVICE.getTodayValue();
  auto it = dailyReadingSeconds.find(today);
  return (it != dailyReadingSeconds.end()) ? it->second : 0;
}

std::vector<DailyStat> ReadingStatsStore::getRecentDays(int limit) const {
  std::vector<DailyStat> result;
  uint32_t today = TIME_SERVICE.getTodayValue();
  if (today == 0) return result;

  for (int i = 0; i < limit; ++i) {
    uint32_t date = offsetDay(today, - (limit - 1 - i));
    uint32_t seconds = 0;
    auto it = dailyReadingSeconds.find(date);
    if (it != dailyReadingSeconds.end()) {
      seconds = it->second;
    }
    result.push_back({date, seconds});
  }
  return result;
}

uint16_t ReadingStatsStore::getCurrentStreakDays() const {
  uint32_t today = TIME_SERVICE.getTodayValue();
  if (today == 0) return 0;

  uint16_t streak = 0;
  uint32_t current = today;
  
  // If no reading today, check starting from yesterday
  if (dailyReadingSeconds.count(today) == 0 || dailyReadingSeconds.at(today) == 0) {
    current = offsetDay(today, -1);
  }

  while (dailyReadingSeconds.count(current) > 0 && dailyReadingSeconds.at(current) > 0) {
    streak++;
    current = offsetDay(current, -1);
  }

  return streak;
}

uint16_t ReadingStatsStore::getLifetimeActiveDays() const {
  uint16_t activeDays = 0;
  for (const auto& pair : dailyReadingSeconds) {
    if (pair.second > 0) {
      activeDays++;
    }
  }
  return activeDays;
}

void ReadingStatsStore::pruneBooks() {
  if (books.size() <= 36) return;

  // 1. Get all books
  std::vector<std::string> filenames;
  for (const auto& pair : books) {
    filenames.push_back(pair.first);
  }

  // 2. Sort by reading time descending to identify top 10
  std::sort(filenames.begin(), filenames.end(), [this](const std::string& a, const std::string& b) {
    return books.at(a).readingSeconds > books.at(b).readingSeconds;
  });

  // 3. Candidates for deletion are those NOT in top 10
  std::vector<std::string> candidates;
  for (size_t i = 10; i < filenames.size(); ++i) {
    candidates.push_back(filenames[i]);
  }

  if (candidates.empty()) return;

  // 4. Sort candidates by lastReadDate ascending (oldest first)
  std::sort(candidates.begin(), candidates.end(), [this](const std::string& a, const std::string& b) {
    return books.at(a).lastReadDate < books.at(b).lastReadDate;
  });

  // 5. Delete the oldest one
  books.erase(candidates.front());
}
