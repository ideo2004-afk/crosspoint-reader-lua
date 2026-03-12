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

  // Record daily stats
  uint32_t today = TIME_SERVICE.getTodayValue(); // YYYYMMDD
  if (today > 0) {
    dailyReadingSeconds[today] += seconds;
    
    // Cleanup: Keep only last 30 days (though we only display 7)
    if (dailyReadingSeconds.size() > 30) {
      dailyReadingSeconds.erase(dailyReadingSeconds.begin());
    }
  }
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
