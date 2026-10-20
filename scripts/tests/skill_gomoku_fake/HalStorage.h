#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct HalFile {
  std::vector<uint8_t>* bytes = nullptr;
  size_t offset = 0;
  bool fail = false;
  size_t size() const { return bytes ? bytes->size() : 0; }
  size_t read(uint8_t* out, size_t length) {
    size_t count = std::min(length, bytes->size() - offset);
    std::memcpy(out, bytes->data() + offset, count);
    offset += count;
    return count;
  }
  size_t write(const uint8_t* input, size_t length) {
    if (fail) return 0;
    bytes->assign(input, input + length);
    return length;
  }
  void flush() {}
};
struct FakeStorage {
  std::map<std::string, std::vector<uint8_t>> files;
  bool failWrite = false, failReplace = false;
  bool ensureDirectoryExists(const char*) { return true; }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    auto found = files.find(path);
    if (found == files.end()) return false;
    file.bytes = &found->second;
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file.bytes = &files[path];
    file.bytes->clear();
    file.fail = failWrite;
    return true;
  }
  bool replaceFile(const char* temporary, const char* path) {
    files.erase(path);  // FAT's remove-before-rename window
    if (failReplace) return false;
    files[path] = files.at(temporary);
    files.erase(temporary);
    return true;
  }
};
inline FakeStorage Storage;
