#pragma once
#include <string>
#include <vector>
#include <functional>

class LibraryStore;
class Stream;

struct LibraryBook {
    std::string path;
    std::string storageDir; // e.g. "epub_12345678"

    bool operator==(const LibraryBook& other) const { return path == other.path; }
};

namespace JsonSettingsIO {
bool loadLibrary(LibraryStore& libStore, Stream& jsonStream);
}

class LibraryStore {
    static LibraryStore instance;
    std::vector<LibraryBook> books;

    friend bool JsonSettingsIO::loadLibrary(LibraryStore& libStore, Stream& jsonStream);

public:
    ~LibraryStore() = default;

    static LibraryStore& getInstance() { return instance; }

    bool loadFromFile();
    bool saveToFile() const;
    // Scan and index books in a specific folder (non-recursive)
    void scanFolder(const std::string& folderPath);

    const std::vector<LibraryBook>& getBooks() const { return books; }
    int getCount() const { return static_cast<int>(books.size()); }

    // Check if a book is already in the library
    bool exists(const std::string& path) const;

    // Recreate missing cache directories for all known books (fast, no full scan)
    void ensureCacheDirectories() const;

    // Get the deterministic storage directory for a given path
    static std::string getStorageDirForPath(const std::string& path);

private:
    void addEntry(const std::string& path);
};

#define LIBRARY_STORE LibraryStore::getInstance()
