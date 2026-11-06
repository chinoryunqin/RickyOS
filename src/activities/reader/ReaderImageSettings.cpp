#include "ReaderImageSettings.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"

namespace {
// Cold-path path/JSON storage only; no resident map of the user's library.
void settingsPath(const std::string& book, char (&path)[80]) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : book) hash = (hash ^ c) * 1099511628211ull;
  snprintf(path, sizeof(path), "/.crosspoint/image-%016llx.json", static_cast<unsigned long long>(hash));
}

bool writeBook(const std::string& book, const ReaderImageProfile& profile) {
  char path[80];
  settingsPath(book, path);
  JsonDocument doc;
  doc["version"] = readerImageBrightness::kScaleVersion;
  doc["book"] = book.c_str();
  doc["contrast"] = profile.contrast;
  doc["brightness"] = profile.brightness;
  doc["smooth"] = profile.smooth;
  if (doc.overflowed()) {
    LOG_ERR("IMG", "OOM building book image settings");
    return false;
  }
  return PersistableStoreBase::writeDocToFile(path, doc);
}

void setDefaults(const ReaderImageProfile& profile) {
  SETTINGS.readerImageContrast = profile.contrast;
  SETTINGS.readerImageBrightness = profile.brightness;
  SETTINGS.imageScaling =
      profile.smooth ? CrossPointSettings::IMAGE_SCALING_BILINEAR : CrossPointSettings::IMAGE_SCALING_NEAREST;
}
}  // namespace

ReaderImageProfile readerImageSettings::defaults() {
  return {SETTINGS.readerImageContrast, SETTINGS.readerImageBrightness,
          SETTINGS.imageScaling == CrossPointSettings::IMAGE_SCALING_BILINEAR};
}

ReaderImageDraft readerImageSettings::load(const std::string& bookPath) {
  ReaderImageDraft result{defaults(), ReaderImageScope::FollowDefault};
  char path[80];
  settingsPath(bookPath, path);
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path, doc)) return result;
  const char* book = doc["book"] | "";
  const int version = doc["version"] | 0;
  if ((version != 1 && version != readerImageBrightness::kScaleVersion) || bookPath != book) return result;
  result.profile.contrast = static_cast<int8_t>(std::clamp(doc["contrast"] | 0, -50, 50));
  const int savedBrightness = doc["brightness"] | 0;
  result.profile.brightness = static_cast<int8_t>(version == 1 ? readerImageBrightness::fromLegacy(savedBrightness)
                                                               : std::clamp(savedBrightness, -40, 40));
  result.profile.smooth = doc["smooth"] | false;
  result.scope = ReaderImageScope::Book;
  return result;
}

bool readerImageSettings::save(const std::string& bookPath, const ReaderImageDraft& draft) {
  char path[80];
  settingsPath(bookPath, path);
  switch (draft.scope) {
    case ReaderImageScope::Book:
      return writeBook(bookPath, draft.profile);
    case ReaderImageScope::FollowDefault:
      return !Storage.exists(path) || Storage.remove(path);
    case ReaderImageScope::Default: {
      const auto previous = defaults();
      setDefaults(draft.profile);
      if (!SETTINGS.saveToFile()) {
        setDefaults(previous);
        return false;
      }
      if (!Storage.exists(path) || Storage.remove(path)) return true;
      // Two independent stores: if clearing the book override fails, restore
      // the previous defaults and report failure, leaving this book untouched.
      setDefaults(previous);
      if (!SETTINGS.saveToFile())
        LOG_ERR("IMG", "Failed to roll back image defaults after book override removal failed");
      return false;
    }
  }
  return false;
}
