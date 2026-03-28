#pragma once
#include <string>
#include <vector>
#include <functional>

class LibraryStore;
class Stream;

struct LibraryBook {
    std::string path;
    std::string title;
    std::string author;
    std::string storageDir; // e.g. "epub_12345678"
    uint32_t fileSize = 0;
    bool hasSmallThumb = false;
    bool hasLargeThumb = false;

    bool operator==(const LibraryBook& other) const { return path == other.path; }
};

namespace JsonSettingsIO {
bool loadLibrary(LibraryStore& libStore, Stream& jsonStream);
}

class LibraryStore {
    static LibraryStore instance;
    std::vector<LibraryBook> books;
    bool scanned = false;  // True after first scan completes

    friend bool JsonSettingsIO::loadLibrary(LibraryStore& libStore, Stream& jsonStream);

public:
    ~LibraryStore() = default;

    static LibraryStore& getInstance() { return instance; }

    bool loadFromFile();
    bool saveToFile() const;

    // Recursively scan /books folder and update the list
    void scan(std::function<void(const std::string&, int)> onProgress = nullptr);

    // Remove books that no longer exist on disk
    void cleanupMissing();

    const std::vector<LibraryBook>& getBooks() const { return books; }
    int getCount() const { return static_cast<int>(books.size()); }

    // Check if a book is already in the library
    bool exists(const std::string& path) const;

    // Returns true if scan() has completed at least once this session
    bool isScanned() const { return scanned; }

    // Reset scan state (e.g. after cache clear)
    void resetScanned() { scanned = false; }

    // Recreate missing cache directories for all known books (fast, no full scan)
    void ensureCacheDirectories() const;

private:
    void scanRecursive(const std::string& path);
    LibraryBook extractMetadata(const std::string& path) const;
};

#define LIBRARY_STORE LibraryStore::getInstance()
