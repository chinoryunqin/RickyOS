#pragma once

class AchievementsStore;
class ReadingStatsStore;

namespace JsonSettingsIO {

enum class ReadingStatsLoadResult { Loaded, Invalid, Unavailable };
bool saveReadingStats(const ReadingStatsStore& store, const char* path, const char* backupPath = nullptr);
ReadingStatsLoadResult loadReadingStatsFromFile(ReadingStatsStore& store, const char* path);

bool saveAchievements(const AchievementsStore& store, const char* path);
bool loadAchievements(AchievementsStore& store, const char* json);
bool loadAchievementsFromFile(AchievementsStore& store, const char* path);

}  // namespace JsonSettingsIO
