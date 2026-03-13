#pragma once

#include <string>
#include <vector>
#include "RecentBooksStore.h"

class PathRepairManager {
 public:
  /**
   * Tries to find a moved file by its filename and size in the /books directory.
   * @param oldPath The previous path of the file.
   * @param fileSize The expected size of the file.
   * @return The new absolute path if found, otherwise an empty string.
   */
  static std::string findMovedFile(const std::string& oldPath, uint32_t fileSize);

  /**
   * Updates all internal records and renames cache directories after a file has been moved.
   * @param oldPath The previous path of the file.
   * @param newPath The new path of the file.
   * @return true if repair was successful.
   */
  static bool repairPath(const std::string& oldPath, const std::string& newPath);

 private:
  static std::string recursiveSearch(const std::string& currentDir, const std::string& targetFilename,
                                     uint32_t targetSize);
  static std::string getCacheDir(const std::string& path);
};
