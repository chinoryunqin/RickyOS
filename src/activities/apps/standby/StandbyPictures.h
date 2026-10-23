#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The pictures Standby and power-off can show, their gallery thumbnails, and which
// picture each screen was last set from. /sleep.bmp and /poweroff.bmp are converted
// copies; the gallery marks the source they came from.
namespace StandbyPictures {

enum class Target : uint8_t { Standby, PowerOff };
enum class Filter : uint8_t { All, Defaults, Mine };

struct Entry {
  std::string path;
  uint32_t size = 0;  // part of the thumbnail key: a replaced file under the same name converts again
};

// Gallery order: the reader's own pictures first, then the downloaded defaults; names
// sorted within each. /images and its subfolders one level down, never the stickers.
// At most kMaxPictures. False when /images cannot be read (an empty list is fine).
constexpr int kMaxPictures = 300;
bool list(Filter filter, std::vector<Entry>& out);

// The cached thumbnail for `entry` at `width`×`height` (filled and cropped like a
// cover). No SD access; the file may not exist yet.
std::string thumbnailPath(const Entry& entry, int width, int height);
// Make the thumbnail if it is missing. Borrows the caller's buffers only through the
// converters, which the caller runs under a FrameBufferLoan. False when the picture
// cannot be decoded.
bool ensureThumbnail(const Entry& entry, int width, int height);

// The picture a target was last set from, empty when unknown (set before this record).
std::string currentSource(Target target);
void rememberSource(Target target, const std::string& path);

}  // namespace StandbyPictures
