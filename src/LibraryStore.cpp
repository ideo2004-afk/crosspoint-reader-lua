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
    FsFile file;
    if (!Storage.openFileForRead("LIB", LIBRARY_FILE_JSON, file)) {
        LOG_INF("LIB", "No library.json found, starting fresh");
        return false;
    }
    bool success = JsonSettingsIO::loadLibrary(*this, file);
    file.close();
    return success;
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

void LibraryStore::scanFolder(const std::string& folderPath) {
    LOG_INF("LIB", "Lazy scanning folder: %s", folderPath.c_str());
    
    auto root = Storage.open(folderPath.c_str());
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    bool changed = false;
    char name[256];
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
        delay(1); // Yield to prevent watchdog
        file.getName(name, sizeof(name));
        
        // Skip hidden files and special directories
        if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
            file.close();
            continue;
        }

        if (!file.isDirectory()) {
            std::string fullPath = folderPath;
            if (fullPath.back() != '/') fullPath += "/";
            fullPath += name;

            if (StringUtils::checkFileExtension(fullPath, ".epub") || 
                StringUtils::checkFileExtension(fullPath, ".xtc") || 
                StringUtils::checkFileExtension(fullPath, ".xtch") ||
                StringUtils::checkFileExtension(fullPath, ".txt") ||
                StringUtils::checkFileExtension(fullPath, ".md")) {
                
                if (!exists(fullPath)) {
                    addEntry(fullPath);
                    changed = true;
                }
            }
        }
        file.close();
    }
    root.close();

    if (changed) {
        saveToFile();
    }
}

void LibraryStore::addEntry(const std::string& path) {
    LibraryBook book;
    book.path = path;
    
    // Determine storage directory (must match Epub/Xtc hashing logic)
    std::string type = "txt";
    if (StringUtils::checkFileExtension(path, ".epub")) type = "epub";
    else if (StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch")) type = "xtc";
    
    book.storageDir = type + "_" + std::to_string(std::hash<std::string>{}(path));
    
    // Ensure cache directory exists
    std::string fullCachePath = "/.crosspoint/" + book.storageDir;
    if (!Storage.exists(fullCachePath.c_str())) {
        Storage.mkdir(fullCachePath.c_str());
    }

    LOG_DBG("LIB", "Indexed new book: %s -> %s", path.c_str(), book.storageDir.c_str());
    books.push_back(book);
}

void LibraryStore::ensureCacheDirectories() const {
    for (const auto& book : books) {
        if (!book.storageDir.empty()) {
            std::string fullCachePath = "/.crosspoint/" + book.storageDir;
            if (!Storage.exists(fullCachePath.c_str())) {
                Storage.mkdir(fullCachePath.c_str());
                LOG_DBG("LIB", "Recreated cache dir: %s", fullCachePath.c_str());
            }
        }
    }
}
