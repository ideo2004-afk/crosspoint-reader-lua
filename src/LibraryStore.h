#pragma once
#include <string>
#include <vector>

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

    friend bool JsonSettingsIO::loadLibrary(LibraryStore& libStore, Stream& jsonStream);

public:
    ~LibraryStore() = default;

    static LibraryStore& getInstance() { return instance; }

    bool loadFromFile();
    bool saveToFile() const;

    // Recursively scan /books folder and update the list
    void scan();

    // Remove books that no longer exist on disk
    void cleanupMissing();

    const std::vector<LibraryBook>& getBooks() const { return books; }
    int getCount() const { return static_cast<int>(books.size()); }

    // Check if a book is already in the library
    bool exists(const std::string& path) const;

private:
    void scanRecursive(const std::string& path);
    LibraryBook extractMetadata(const std::string& path) const;
};

#define LIBRARY_STORE LibraryStore::getInstance()
