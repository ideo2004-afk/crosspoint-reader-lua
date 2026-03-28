#include "LibraryStore.h"
#include <HalStorage.h>
#include <Logging.h>
#include "JsonSettingsIO.h"
#include "Epub.h"
#include "Xtc.h"
#include "Txt.h"
#include "util/StringUtils.h"

LibraryStore LibraryStore::instance;

constexpr char LIBRARY_FILE_JSON[] = "/.crosspoint/library.json";

bool LibraryStore::loadFromFile() {
    String json = Storage.readFile(LIBRARY_FILE_JSON);
    if (json.length() == 0) {
        LOG_INF("LIB", "No library.json found, starting fresh");
        return false;
    }
    return JsonSettingsIO::loadLibrary(*this, json.c_str());
}

bool LibraryStore::saveToFile() const {
    return JsonSettingsIO::saveLibrary(*this, LIBRARY_FILE_JSON);
}

bool LibraryStore::exists(const std::string& path) const {
    for (const auto& book : books) {
        if (book.path == path) return true;
    }
    return false;
}

void LibraryStore::scan() {
    LOG_INF("LIB", "Scanning library in /books...");
    cleanupMissing(); // Remove deleted files first
    scanRecursive("/books");
    saveToFile();
    LOG_INF("LIB", "Scan complete. %d books indexed.", getCount());
}

void LibraryStore::scanRecursive(const std::string& path) {
    auto root = Storage.open(path.c_str());
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    char name[256];
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
        file.getName(name, sizeof(name));
        
        // Skip hidden files and special directories
        if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
            file.close();
            continue;
        }

        std::string fullPath = path;
        if (fullPath.back() != '/') fullPath += "/";
        fullPath += name;

        if (file.isDirectory()) {
            file.close();
            scanRecursive(fullPath);
        } else {
            file.close(); // Close before processing to save file handles
            
            if (exists(fullPath)) continue;

            if (StringUtils::checkFileExtension(fullPath, ".epub") || 
                StringUtils::checkFileExtension(fullPath, ".xtc") || 
                StringUtils::checkFileExtension(fullPath, ".xtch") ||
                StringUtils::checkFileExtension(fullPath, ".txt") ||
                StringUtils::checkFileExtension(fullPath, ".md")) {
                
                LOG_DBG("LIB", "Found new book: %s", fullPath.c_str());
                books.push_back(extractMetadata(fullPath));
            }
        }
    }
    root.close();
}

void LibraryStore::cleanupMissing() {
    int removed = 0;
    for (auto it = books.begin(); it != books.end(); ) {
        bool exists = Storage.exists(it->path.c_str());
        bool inBooks = (it->path.find("/books/") == 0);
        
        if (!exists || !inBooks) {
            LOG_INF("LIB", "Removing missing or invalid book: %s", it->path.c_str());
            it = books.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        LOG_INF("LIB", "Cleanup: removed %d entries", removed);
    }
}

LibraryBook LibraryStore::extractMetadata(const std::string& path) const {
    LibraryBook book;
    book.path = path;
    
    FsFile f;
    if (Storage.openFileForRead("LIB", path, f)) {
        book.fileSize = f.size();
        f.close();
    }

    if (StringUtils::checkFileExtension(path, ".epub")) {
        Epub epub(path, "/.crosspoint");
        // Load metadata only, skip CSS
        if (epub.load(true, true)) {
            book.title = epub.getTitle();
            book.author = epub.getAuthor();
        }
    } else if (StringUtils::checkFileExtension(path, ".xtc") || 
               StringUtils::checkFileExtension(path, ".xtch")) {
        Xtc xtc(path, "/.crosspoint");
        if (xtc.load()) {
            book.title = xtc.getTitle();
            book.author = xtc.getAuthor();
        }
    } else {
        // TXT / MD: Use filename as title
        const auto lastSlash = path.find_last_of('/');
        const auto lastDot = path.find_last_of('.');
        if (lastSlash != std::string::npos && lastDot != std::string::npos && lastDot > lastSlash) {
            book.title = path.substr(lastSlash + 1, lastDot - lastSlash - 1);
        } else {
            book.title = path;
        }
    }

    if (book.title.empty()) {
        const auto lastSlash = path.find_last_of('/');
        book.title = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    }

    return book;
}
