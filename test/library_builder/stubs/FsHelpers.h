#pragma once

#include <cctype>
#include <cstring>
#include <string>

namespace FsHelpers {
// Case-insensitive, like the firmware implementation (BOOK.TXT is a book).
inline bool checkFileExtension(const std::string& path, const char* extension) {
  const size_t length = strlen(extension);
  if (path.size() < length) return false;
  for (size_t i = 0; i < length; ++i) {
    if (tolower(static_cast<unsigned char>(path[path.size() - length + i])) !=
        tolower(static_cast<unsigned char>(extension[i])))
      return false;
  }
  return true;
}
inline bool hasEpubExtension(const std::string& path) { return checkFileExtension(path, ".epub"); }
// Same rule as the firmware: EPUB, TXT and Markdown (+ MOBI/AZW3 with RICKYOS_KINDLE_IMPORT).
inline bool hasReflowableBookExtension(const std::string& path) {
#if defined(RICKYOS_KINDLE_IMPORT)
  if (checkFileExtension(path, ".mobi") || checkFileExtension(path, ".azw3")) return true;
#endif
  return hasEpubExtension(path) || checkFileExtension(path, ".txt") || checkFileExtension(path, ".md");
}
inline bool isProtectedPathComponent(const std::string& component) {
  return component.empty() || component.front() == '.' || component == "System Volume Information" ||
         component == "XTCache";
}
}  // namespace FsHelpers
