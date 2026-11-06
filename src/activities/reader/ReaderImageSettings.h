#pragma once

#include <Epub/converters/ImageToneCurve.h>

#include <cstdint>
#include <string>

#include "util/ReaderImageBrightness.h"

struct ReaderImageProfile {
  int8_t contrast = 0;
  int8_t brightness = 0;
  bool smooth = false;
  bool operator==(const ReaderImageProfile&) const = default;
  int renderBrightness() const { return readerImageBrightness::toRender(brightness); }
  uint32_t identity() const { return ImageToneCurve::profileIdentity(contrast, renderBrightness(), smooth); }
};

enum class ReaderImageScope : uint8_t { Book, Default, FollowDefault };
struct ReaderImageDraft {
  ReaderImageProfile profile;
  ReaderImageScope scope = ReaderImageScope::FollowDefault;
};

namespace readerImageSettings {
ReaderImageProfile defaults();
ReaderImageDraft load(const std::string& bookPath);
bool save(const std::string& bookPath, const ReaderImageDraft& draft);
}  // namespace readerImageSettings
