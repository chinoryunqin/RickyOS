#include "NativeFontMetadata.h"

#if RICKYOS_METALIO_FONT_METADATA
#include <Logging.h>
#include <esp_heap_caps.h>
#include <uzlib.h>

#include <cstddef>
#include <cstdint>

#include "EpdFontData.h"

namespace {
struct NativeMetadataTable {
  const uint8_t* source;
  size_t compressedSize;
  size_t offset;
  size_t size;
  size_t stride;
  uint32_t checksum;
};
struct NativeFontOffsets {
  size_t glyphs;
  size_t intervals;
  uint32_t intervalCount;
};
#include <NativeFontMetadata.generated.inc>

// This offline face lives for the whole boot. The table pool is PSRAM-only
// and capped by the generator, preserving internal display/USB/radio reserves.
uint8_t* metadataPool = nullptr;

bool decode(const NativeMetadataTable& table, uint8_t* pool) {
  TINF_DATA inflater{};
  uzlib_uncompress_init(&inflater, nullptr, 0);
  auto* output = pool + table.offset;
  inflater.source = table.source;
  inflater.source_limit = table.source + table.compressedSize;
  inflater.dest_start = output;
  inflater.dest = output;
  inflater.dest_limit = output + table.size + 1;
  if (uzlib_uncompress(&inflater) != TINF_DONE || inflater.dest != output + table.size ||
      inflater.source != inflater.source_limit) {
    return false;
  }
  for (size_t i = table.stride; i < table.size; ++i) output[i] += output[i - table.stride];
  uint32_t checksum = 2166136261U;
  for (size_t i = 0; i < table.size; ++i) checksum = (checksum ^ output[i]) * 16777619U;
  return checksum == table.checksum;
}
}  // namespace

bool NativeFontMetadata::initialize(EpdFontData& regular) {
  if (!metadataPool) {
    auto* pool = static_cast<uint8_t*>(heap_caps_malloc(kNativeFontMetadataBytes, MALLOC_CAP_SPIRAM));
    if (!pool) {
      LOG_ERR("FONT", "OOM allocating native font metadata (%u PSRAM bytes)",
              static_cast<unsigned>(kNativeFontMetadataBytes));
      return false;
    }
    for (const auto& table : kNativeMetadataTables) {
      if (!decode(table, pool)) {
        heap_caps_free(pool);
        LOG_ERR("FONT", "Native font metadata validation failed");
        return false;
      }
    }
    metadataPool = pool;
  }
  const auto& offsets = kNativeFontOffsets[0];
  regular.glyph = reinterpret_cast<const EpdGlyph*>(metadataPool + offsets.glyphs);
  regular.intervals = reinterpret_cast<const EpdUnicodeInterval*>(metadataPool + offsets.intervals);
  regular.intervalCount = offsets.intervalCount;
  return true;
}
#endif
