#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <string>
#include <utility>

#include "AchievementsStore.h"
#include "ReadingStatsStore.h"
#include "util/BookIdentity.h"

namespace {

bool saveJsonDocumentToFile(const char* moduleName, const char* path, const JsonDocument& doc,
                            const char* backupPath = nullptr) {
  if (!path || *path == '\0') {
    LOG_ERR(moduleName, "Missing JSON path for write");
    return false;
  }
  if (doc.overflowed()) {
    LOG_ERR(moduleName, "Insufficient heap for JSON document");
    return false;
  }

  const size_t pathLength = strlen(path);
  const size_t tempPathSize = pathLength + sizeof(".tmp");
  // Export paths are variable-length, so use one fallible exact-size buffer instead of fixed stack storage.
  auto tempPath = makeUniqueNoThrow<char[]>(tempPathSize);
  if (!tempPath) {
    LOG_ERR(moduleName, "OOM allocating JSON temp path (%zu bytes)", tempPathSize);
    return false;
  }
  memcpy(tempPath.get(), path, pathLength);
  memcpy(tempPath.get() + pathLength, ".tmp", sizeof(".tmp"));

  const HalStorage::StorageLock lock;
  bool tempComplete = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite(moduleName, tempPath.get(), file)) {
      LOG_ERR(moduleName, "Could not open JSON file for write: %s", tempPath.get());
      return false;
    }

    const size_t written = serializeJson(doc, file);
    const bool closed = file.close();
    tempComplete = closed && written == measureJson(doc);
  }
  if (!tempComplete) {
    Storage.remove(tempPath.get());
    LOG_ERR(moduleName, "Incomplete JSON write: %s", tempPath.get());
    return false;
  }

  // Keep the previous committed file on FAT while publishing the new one.
  if (backupPath && Storage.exists(path)) {
    if ((Storage.exists(backupPath) && !Storage.remove(backupPath)) || !Storage.rename(path, backupPath)) {
      LOG_ERR(moduleName, "Could not back up JSON file: %s", path);
      return false;
    }
  }
  const bool published =
      backupPath ? Storage.rename(tempPath.get(), path) : Storage.replaceFile(tempPath.get(), path);
  if (!published) {
    LOG_ERR(moduleName, "Could not publish JSON temp file: %s", path);
    return false;
  }

  return true;
}

class ReadingStatsJsonReader {
 public:
  explicit ReadingStatsJsonReader(HalFile& file) : size(file.size()), reader(file, 512) {}

  int read() {
    uint8_t byte;
    return reader.read(&byte, 1) == 1 ? byte : -1;
  }

  size_t readBytes(char* buffer, size_t length) { return reader.read(buffer, length); }
  bool reachedEnd() const { return reader.position() == size; }

 private:
  const size_t size;
  serialization::BufferedFileReader reader;
};

}  // namespace

// ---- ReadingStatsStore ----
// reading_stats.json, format version 6. Stream directly from the SD card to
// avoid the whole-file helper's size limit and a second full copy in memory.

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path, const char* backupPath) {
  JsonDocument doc;
  doc["formatVersion"] = 6;

  JsonArray days = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.getReadingDays()) {
    JsonObject dayObj = days.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray legacyDays = doc["legacyReadingDays"].to<JsonArray>();
  for (const auto& day : store.legacyReadingDays) {
    JsonObject dayObj = legacyDays.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray sessionLog = doc["sessionLog"].to<JsonArray>();
  for (const auto& session : store.getSessionLog()) {
    JsonObject sessionObj = sessionLog.add<JsonObject>();
    sessionObj["dayOrdinal"] = session.dayOrdinal;
    sessionObj["sessionMs"] = session.sessionMs;
  }

  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = books.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    JsonArray knownPaths = obj["knownPaths"].to<JsonArray>();
    for (const auto& knownPath : book.knownPaths) {
      knownPaths.add(knownPath);
    }
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["chapterTitle"] = book.chapterTitle;
    obj["totalReadingMs"] = book.totalReadingMs;
    obj["sessions"] = book.sessions;
    obj["lastSessionMs"] = book.lastSessionMs;
    obj["firstReadAt"] = book.firstReadAt;
    obj["lastReadAt"] = book.lastReadAt;
    obj["completedAt"] = book.completedAt;
    obj["lastProgressPercent"] = book.lastProgressPercent;
    obj["chapterProgressPercent"] = book.chapterProgressPercent;
    obj["completed"] = book.completed;

    JsonArray bookDays = obj["readingDays"].to<JsonArray>();
    for (const auto& day : book.readingDays) {
      JsonObject dayObj = bookDays.add<JsonObject>();
      dayObj["dayOrdinal"] = day.dayOrdinal;
      dayObj["readingMs"] = day.readingMs;
    }
  }

  return saveJsonDocumentToFile("RST", path, doc, backupPath);
}

JsonSettingsIO::ReadingStatsLoadResult JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore& store,
                                                                              const char* path) {
  JsonDocument doc;
  {
    HalFile file;
    if (!Storage.openFileForRead("RST", path, file)) {
      return ReadingStatsLoadResult::Unavailable;
    }
    ReadingStatsJsonReader reader(file);
    const auto error = deserializeJson(doc, reader);
    if (error) {
      LOG_ERR("RST", "JSON parse error in %s: %s", path, error.c_str());
      if (error == DeserializationError::NoMemory ||
          ((error == DeserializationError::EmptyInput || error == DeserializationError::IncompleteInput) &&
           !reader.reachedEnd())) {
        return ReadingStatsLoadResult::Unavailable;
      }
      return ReadingStatsLoadResult::Invalid;
    }
    // ArduinoJson stops at the end of the root object. Reject trailing data
    // and distinguish a short card read from reaching the physical end.
    int trailing;
    while ((trailing = reader.read()) == ' ' || trailing == '\n' || trailing == '\r' || trailing == '\t') {
    }
    if (trailing != -1 || !doc["books"].is<JsonArray>()) {
      LOG_ERR("RST", "Invalid reading stats document: %s", path);
      return ReadingStatsLoadResult::Invalid;
    }
    if (!reader.reachedEnd()) {
      LOG_ERR("RST", "Short read of reading stats: %s", path);
      return ReadingStatsLoadResult::Unavailable;
    }
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();
  store.sessionLog.clear();
  store.dirty = false;

  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  auto appendReadingDays = [](std::vector<ReadingDayStats>& destination, JsonArray source) {
    for (JsonVariant value : source) {
      ReadingDayStats day;
      if (value.is<JsonObject>()) {
        JsonObject obj = value.as<JsonObject>();
        day.dayOrdinal = obj["dayOrdinal"] | static_cast<uint32_t>(0);
        day.readingMs = obj["readingMs"] | static_cast<uint64_t>(0);
      } else {
        day.dayOrdinal = value | static_cast<uint32_t>(0);
        day.readingMs = 0;
      }
      if (day.dayOrdinal != 0) {
        destination.push_back(day);
      }
    }
  };

  appendReadingDays(store.readingDays, doc["readingDays"].as<JsonArray>());
  if (formatVersion >= 2) {
    appendReadingDays(store.legacyReadingDays, doc["legacyReadingDays"].as<JsonArray>());
    if (formatVersion < 6 && store.legacyReadingDays.empty()) {
      store.legacyReadingDays = store.readingDays;
    }
  } else {
    store.legacyReadingDays = store.readingDays;
  }

  if (formatVersion >= 4) {
    for (JsonObject sessionObj : doc["sessionLog"].as<JsonArray>()) {
      ReadingSessionLogEntry session;
      session.dayOrdinal = sessionObj["dayOrdinal"] | static_cast<uint32_t>(0);
      session.sessionMs = sessionObj["sessionMs"] | static_cast<uint32_t>(0);
      if (session.dayOrdinal != 0 && session.sessionMs != 0) {
        store.sessionLog.push_back(session);
      }
    }
  } else {
    store.dirty = true;
  }

  JsonArray books = doc["books"].as<JsonArray>();
  for (JsonObject obj : books) {
    ReadingBookStats book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
    }
    for (JsonVariant value : obj["knownPaths"].as<JsonArray>()) {
      const std::string knownPath = value | std::string("");
      if (!knownPath.empty()) {
        book.knownPaths.push_back(knownPath);
      }
    }
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.chapterTitle = obj["chapterTitle"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | static_cast<uint64_t>(0);
    book.sessions = obj["sessions"] | static_cast<uint32_t>(0);
    book.lastSessionMs = obj["lastSessionMs"] | static_cast<uint32_t>(0);
    book.firstReadAt = obj["firstReadAt"] | static_cast<uint32_t>(0);
    book.lastReadAt = obj["lastReadAt"] | static_cast<uint32_t>(0);
    book.completedAt = obj["completedAt"] | static_cast<uint32_t>(0);
    book.lastProgressPercent = obj["lastProgressPercent"] | static_cast<uint8_t>(0);
    book.chapterProgressPercent = obj["chapterProgressPercent"] | static_cast<uint8_t>(0);
    book.completed = obj["completed"] | false;
    if (formatVersion >= 2) {
      appendReadingDays(book.readingDays, obj["readingDays"].as<JsonArray>());
    }
    if (formatVersion < 3 || book.bookId.empty()) {
      store.dirty = true;
    }
    store.books.push_back(std::move(book));
  }

  if (formatVersion < 6) {
    store.convertLegacyReadingDaysToUnassigned();
    store.dirty = true;
  }
  store.rebuildAggregatedReadingDays();
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return ReadingStatsLoadResult::Loaded;
}

// ---- AchievementsStore ----
// achievements.json, format version 2. Written atomically (temp + rename).

bool JsonSettingsIO::saveAchievements(const AchievementsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  doc["accumulatedReadingMs"] = store.accumulatedReadingMs;
  doc["countedSessions"] = store.countedSessions;
  doc["totalBookmarksAdded"] = store.totalBookmarksAdded;
  doc["longestSessionMs"] = store.longestSessionMs;
  doc["goalDaysCount"] = store.goalDaysCount;
  doc["currentGoalStreak"] = store.currentGoalStreak;
  doc["maxGoalStreak"] = store.maxGoalStreak;
  doc["lastGoalDayOrdinal"] = store.lastGoalDayOrdinal;
  doc["resetDayOrdinal"] = store.resetDayOrdinal;
  doc["resetDayBaselineMs"] = store.resetDayBaselineMs;

  JsonArray states = doc["states"].to<JsonArray>();
  for (const auto& state : store.states) {
    JsonObject obj = states.add<JsonObject>();
    obj["unlocked"] = state.unlocked;
    obj["unlockedAt"] = state.unlockedAt;
  }

  JsonArray startedBooks = doc["startedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.startedBooks) {
    startedBooks.add(pathValue);
  }

  JsonArray finishedBooks = doc["finishedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.finishedBooks) {
    finishedBooks.add(pathValue);
  }

  return saveJsonDocumentToFile("ACH", path, doc);
}

bool JsonSettingsIO::loadAchievements(AchievementsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("ACH", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.states = {};
  store.startedBooks.clear();
  store.finishedBooks.clear();
  store.pendingUnlocks.clear();
  store.dirty = false;
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  store.accumulatedReadingMs = doc["accumulatedReadingMs"] | static_cast<uint64_t>(0);
  store.countedSessions = doc["countedSessions"] | static_cast<uint32_t>(0);
  store.totalBookmarksAdded = doc["totalBookmarksAdded"] | static_cast<uint32_t>(0);
  store.longestSessionMs = doc["longestSessionMs"] | static_cast<uint32_t>(0);
  store.goalDaysCount = doc["goalDaysCount"] | static_cast<uint32_t>(0);
  store.currentGoalStreak = doc["currentGoalStreak"] | static_cast<uint32_t>(0);
  store.maxGoalStreak = doc["maxGoalStreak"] | static_cast<uint32_t>(0);
  store.lastGoalDayOrdinal = doc["lastGoalDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayOrdinal = doc["resetDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayBaselineMs = doc["resetDayBaselineMs"] | static_cast<uint64_t>(0);
  // Session serials are runtime-only; persisted values collide after ReadingStatsStore resets on reboot.
  store.lastProcessedSessionSerial = 0;

  JsonArray states = doc["states"].as<JsonArray>();
  size_t stateIndex = 0;
  for (JsonObject obj : states) {
    if (stateIndex >= store.states.size()) {
      break;
    }
    store.states[stateIndex].unlocked = obj["unlocked"] | false;
    store.states[stateIndex].unlockedAt = obj["unlockedAt"] | static_cast<uint32_t>(0);
    ++stateIndex;
  }

  for (JsonVariant value : doc["startedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.startedBooks.push_back(bookKey);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.finishedBooks.push_back(bookKey);
    }
  }

  return true;
}

bool JsonSettingsIO::loadAchievementsFromFile(AchievementsStore& store, const char* path) {
  if (!Storage.exists(path)) {
    return false;
  }
  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    return false;
  }
  return loadAchievements(store, json.c_str());
}
