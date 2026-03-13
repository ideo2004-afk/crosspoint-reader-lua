#include "PathRepairManager.h"
#include <HalStorage.h>
#include <Logging.h>
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "util/StringUtils.h"

std::string PathRepairManager::findMovedFile(const std::string& oldPath, uint32_t fileSize) {
  if (fileSize == 0) return ""; // Cannot safely match if size is unknown

  size_t lastSlash = oldPath.find_last_of('/');
  std::string filename = (lastSlash == std::string::npos) ? oldPath : oldPath.substr(lastSlash + 1);

  LOG_INF("PRM", "Searching for moved file: %s (%u bytes)", filename.c_str(), fileSize);
  return recursiveSearch("/books", filename, fileSize);
}

std::string PathRepairManager::recursiveSearch(const std::string& currentDir, const std::string& targetFilename,
                                               uint32_t targetSize) {
  FsFile dir = Storage.open(currentDir.c_str());
  if (!dir || !dir.isDirectory()) return "";

  FsFile file;
  while (file = dir.openNextFile(O_RDONLY)) {
    char name[256];
    if (file.getName(name, sizeof(name)) == 0) {
      file.close();
      continue;
    }
    
    std::string fileNameStr = name;
    
    // Skip hidden files/dirs (starting with .)
    if (fileNameStr.length() > 0 && fileNameStr[0] == '.') {
      file.close();
      continue;
    }

    std::string fullPath = currentDir + "/" + fileNameStr;

    if (file.isDirectory()) {
      std::string found = recursiveSearch(fullPath, targetFilename, targetSize);
      if (!found.empty()) {
        file.close();
        dir.close();
        return found;
      }
    } else {
      if (fileNameStr == targetFilename && file.size() == targetSize) {
        LOG_INF("PRM", "Found match: %s", fullPath.c_str());
        file.close();
        dir.close();
        return fullPath;
      }
    }
    file.close();
  }
  dir.close();
  return "";
}

bool PathRepairManager::repairPath(const std::string& oldPath, const std::string& newPath) {
  LOG_INF("PRM", "Repairing records and cache: %s -> %s", oldPath.c_str(), newPath.c_str());

  // 1. Rename cache directory
  std::string oldCache = getCacheDir(oldPath);
  std::string newCache = getCacheDir(newPath);

  if (!oldCache.empty() && !newCache.empty() && Storage.exists(oldCache.c_str())) {
    // If target already exists (unlikely but possible), clear it first
    if (Storage.exists(newCache.c_str())) {
      LOG_INF("PRM", "Target cache exists, removing: %s", newCache.c_str());
      Storage.removeDir(newCache.c_str());
    }
    
    if (Storage.rename(oldCache.c_str(), newCache.c_str())) {
      LOG_INF("PRM", "Renamed cache: %s -> %s", oldCache.c_str(), newCache.c_str());
    } else {
      LOG_ERR("PRM", "Failed to rename cache directory");
    }
  }

  // 2. Update Stores (In-memory)
  RECENT_BOOKS.updatePath(oldPath, newPath);
  READING_STATS.updatePath(oldPath, newPath);

  // 3. Persist changes
  RECENT_BOOKS.saveToFile();
  READING_STATS.saveToFile();

  return true;
}

std::string PathRepairManager::getCacheDir(const std::string& path) {
  const std::string hash = std::to_string(std::hash<std::string>{}(path));
  if (StringUtils::checkFileExtension(path, ".epub")) return "/.crosspoint/epub_" + hash;
  if (StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch"))
    return "/.crosspoint/xtc_" + hash;
  if (StringUtils::checkFileExtension(path, ".txt") || StringUtils::checkFileExtension(path, ".md"))
    return "/.crosspoint/txt_" + hash;
  return "";
}
