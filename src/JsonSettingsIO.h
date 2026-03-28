#pragma once
#include <Stream.h>

class CrossPointSettings;
class CrossPointState;
class WifiCredentialStore;
class RecentBooksStore;
class ReadingStatsStore;
class LibraryStore;

namespace JsonSettingsIO {

// CrossPointSettings
bool saveSettings(const CrossPointSettings& s, const char* path);
bool loadSettings(CrossPointSettings& s, const char* json, bool* needsResave = nullptr);

// CrossPointState
bool saveState(const CrossPointState& s, const char* path);
bool loadState(CrossPointState& s, const char* json);

// WifiCredentialStore
bool saveWifi(const WifiCredentialStore& store, const char* path);
bool loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave = nullptr);

// RecentBooksStore
bool saveRecentBooks(const RecentBooksStore& store, const char* path);
bool loadRecentBooks(RecentBooksStore& store, const char* json);

// ReadingStatsStore
bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);

// LibraryStore
bool saveLibrary(const LibraryStore& libStore, const char* path);
bool loadLibrary(LibraryStore& libStore, Stream& jsonStream);

}  // namespace JsonSettingsIO
