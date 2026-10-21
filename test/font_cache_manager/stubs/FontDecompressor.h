#pragma once

#include <cstdint>
#include <cstdio>

#include "EpdFontFamily.h"

class FontDecompressor {
 public:
  struct PrewarmCall {
    const EpdFontData* fontData = nullptr;
    char text[32] = {};
  };

  void clearCache() { clearCacheCount++; }
  // Per-page clear: drops the page buffer only, hot groups stay.
  void clearPageCache() { clearPageCacheCount++; }
  int prewarmCache(const EpdFontData* fontData, const char* text) {
    auto& call = prewarmCalls[prewarmCallCount++];
    call.fontData = fontData;
    std::snprintf(call.text, sizeof(call.text), "%s", text);
    return 0;
  }
  void logStats(const char*) {}
  void resetStats() {}

  PrewarmCall prewarmCalls[4] = {};
  int prewarmCallCount = 0;
  int clearCacheCount = 0;
  int clearPageCacheCount = 0;
};
