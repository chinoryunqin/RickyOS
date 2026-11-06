#include "SystemUiFonts.h"

#include <algorithm>

#include "CrossPointSettings.h"
#if RICKYOS_SYSTEM_UI_FONTS
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TtfEpdFont.h>
#include <esp_heap_caps.h>

#include <array>
#include <cstring>

#include "SdCardFontSystem.h"
#include "components/RickyDesignUi.h"
#include "resources/SystemUiFont_sc_regular.h"
#include "resources/SystemUiFont_tc_regular.h"
namespace {
using namespace system_ui_font_data;
constexpr unsigned count = 5;
constexpr int firstId = 0x53554900;
struct Slot {
  std::unique_ptr<TtfEpdFont> font;
};
std::array<Slot, count * 2> slots;
// Share one small PSRAM cache across all sizes. FreeType makes many nearby
// reads of outlines and metrics, especially while measuring a new book title.
struct CustomSource {
  static constexpr unsigned long blockBytes = 512;
  static constexpr size_t blockCount = 32;
  struct Block {
    unsigned long start = 0;
    unsigned long length = 0;
    uint32_t used = 0;
  };
  HalFile file;
  unsigned long size = 0;
  uint8_t* data = nullptr;
  std::array<Block, blockCount> blocks{};
  uint32_t clock = 0;

  void close() {
    file.close();
    heap_caps_free(data);
    data = nullptr;
    size = 0;
    blocks = {};
    clock = 0;
  }
};
CustomSource custom;
char loadedFamily[64] = {}, previewFamily[64] = {};
uint8_t loadedScale = 0, loadedLanguage = 255, previewScale = 100;
bool previewing = false, suspended = false, customOk = false;
constexpr uint8_t sizes[count] = {
    ricky::panel::pixels(10, 8), ricky::panel::pixels(12, 10), ricky::panel::pixels(14, 12),
    ricky::panel::pixels(16, 14), ricky::panel::pixels(18, 16)};
constexpr unsigned bodySlot = 1;
struct Binding {
  int id;
  unsigned slot;
};
constexpr Binding bindings[] = {{SMALL_FONT_ID, 0},           {UI_10_FONT_ID, 2},          {UI_12_FONT_ID, 3},
                                {CONTROL_18_FONT_ID, 4},      {CJK_UI_10_FONT_ID, 0},      {CJK_UI_14_FONT_ID, 2},
                                {CJK_UI_16_FONT_ID, 3},       {RickyDesignUi::CAPTION, 0}, {RickyDesignUi::BODY, bodySlot},
                                {RickyDesignUi::HEADLINE, 2}, {RickyDesignUi::TITLE, 3}};
unsigned long readFont(void* ctx, unsigned long offset, unsigned char* data, unsigned long length) {
  auto& source = *static_cast<CustomSource*>(ctx);
  if (length == 0 || offset >= source.size) return 0;
  length = std::min(length, source.size - offset);
  if (!source.data || length > CustomSource::blockBytes)
    return SdCardFontRegistry::halFileRead(&source.file, offset, data, length);

  unsigned long copied = 0;
  while (copied < length) {
    const unsigned long position = offset + copied;
    const unsigned long start = position - position % CustomSource::blockBytes;
    size_t slot = 0;
    for (size_t i = 0; i < source.blocks.size(); ++i) {
      const auto& block = source.blocks[i];
      if (block.length != 0 && block.start == start) {
        slot = i;
        break;
      }
      if (block.used < source.blocks[slot].used) slot = i;
    }
    auto& block = source.blocks[slot];
    auto* bytes = source.data + slot * CustomSource::blockBytes;
    if (block.length == 0 || block.start != start) {
      const unsigned long want = std::min(CustomSource::blockBytes, source.size - start);
      const auto read = SdCardFontRegistry::halFileRead(&source.file, start, bytes, want);
      // Never retain a partial block after an I/O failure. A later read can retry.
      block = {start, read == want ? read : 0, 0};
      if (read != want)
        return copied + SdCardFontRegistry::halFileRead(&source.file, position, data + copied, length - copied);
    }
    block.used = ++source.clock;
    const unsigned long within = position - start;
    const unsigned long take = std::min(length - copied, block.length - within);
    memcpy(data + copied, bytes + within, take);
    copied += take;
  }
  return copied;
}
void release(GfxRenderer& renderer, unsigned begin, unsigned end) {
  for (unsigned i = begin; i < end; ++i)
    if (slots[i].font) {
      renderer.unregisterTtfFont(firstId + i);
      renderer.removeFont(firstId + i);
      slots[i].font.reset();
    }
}
void bind(GfxRenderer& renderer) {
  for (const auto& b : bindings) {
    const int builtin = firstId + b.slot;
    const int selected = customOk ? builtin + count : builtin;
    if (slots[selected - firstId].font) {
      renderer.setPreferredFont(b.id, selected);
      renderer.setFallbackFont(b.id, builtin, CJK_UI_12_FONT_ID);
    }
  }
  if (customOk) {
    // Book cards have their own bitmap title role. Keep it as the default and
    // let the renderer try it alongside the other fallbacks for missing glyphs.
    const int builtin = firstId + bodySlot;
    renderer.setPreferredFont(RickyDesignUi::BOOK_TITLE, builtin + count);
    renderer.setFallbackFont(RickyDesignUi::BOOK_TITLE, builtin, CJK_UI_12_FONT_ID);
  }
}
}  // namespace
#endif
namespace systemUiFonts {
bool ownsFont(int id) {
#if RICKYOS_SYSTEM_UI_FONTS
  return id >= firstId && id < firstId + static_cast<int>(count * 2);
#else
  return false;
#endif
}
uint8_t scale() {
#if RICKYOS_SYSTEM_UI_FONTS
  if (previewing) return previewScale;
#endif
  return std::clamp<uint8_t>(SETTINGS.uiTextScale, minScale, maxScale);
}
void preview(const char* family, uint8_t size) {
#if RICKYOS_SYSTEM_UI_FONTS
  snprintf(previewFamily, sizeof(previewFamily), "%s", family ? family : "");
  previewScale = std::clamp<uint8_t>(size, minScale, maxScale);
  previewing = true;
#endif
}
void clearPreview() {
#if RICKYOS_SYSTEM_UI_FONTS
  previewing = false;
#endif
}
bool customAvailable() {
#if RICKYOS_SYSTEM_UI_FONTS
  return customOk;
#else
  return false;
#endif
}
void suspend(GfxRenderer& renderer) {
#if RICKYOS_SYSTEM_UI_FONTS
  suspended = true;
  customOk = false;
  release(renderer, count, count * 2);
  custom.close();
  bind(renderer);
#endif
}
void resume() {
#if RICKYOS_SYSTEM_UI_FONTS
  if (suspended) {
    suspended = false;
    loadedFamily[0] = '\0';  // only the SD face was released for standby / USB
  }
#endif
}
void ensureLoaded(GfxRenderer& renderer) {
#if RICKYOS_SYSTEM_UI_FONTS
  const char* family = previewing ? previewFamily : SETTINGS.uiFontFamily;
  const auto language = static_cast<uint8_t>(I18N.getLanguage());
  const auto size = scale();
  const bool builtinChanged = loadedScale != size || loadedLanguage != language;
  if (!builtinChanged && strcmp(loadedFamily, family) == 0) {
    bind(renderer);
    return;
  }
  customOk = false;
  release(renderer, count, count * 2);
  custom.close();
  if (builtinChanged) release(renderer, 0, count);
  snprintf(loadedFamily, sizeof(loadedFamily), "%s", family);
  loadedScale = size;
  loadedLanguage = language;
  const bool tc = I18N.getLanguage() == Language::ZH_TW;
  if (!suspended && family[0] && Storage.ready()) {
    const auto* found = sdFontSystem.registry().findFamily(family);
    if (found && found->vector)
      for (const auto& file : found->files)
        if (file.style == 0) {
          custom.file = Storage.open(file.path.c_str());
          break;
        }
  }
  for (unsigned i = 0; builtinChanged && i < count; ++i) {
    auto font = makeUniqueNoThrow<TtfEpdFont>();
    if (!font) {
      LOG_ERR("UIFONT", "OOM: default face");
      break;
    }
    if (tc) {
      font->addResidentSource(0, tc_regular, sizeof(tc_regular));
    } else {
      font->addResidentSource(0, sc_regular, sizeof(sc_regular));
    }
    const auto pt = static_cast<uint16_t>((sizes[i] * size + 50) / 100);
    if (!font->load(pt, true, 12 * 1024, 384)) {
      LOG_ERR("UIFONT", "Default face failed");
      break;
    }
    renderer.insertFont(firstId + i, font->family(i == 2 || i == 3));
    renderer.registerTtfFont(firstId + i, font.get());
    slots[i].font = std::move(font);
  }
  custom.size = custom.file ? custom.file.size() : 0;
  if (custom.size > 0) {
    // Never spill the cache into internal RAM needed by display, USB and radio.
    custom.data = static_cast<uint8_t*>(
        heap_caps_malloc(CustomSource::blockCount * CustomSource::blockBytes, MALLOC_CAP_SPIRAM));
    customOk = true;
    for (unsigned i = 0; i < count; ++i) {
      auto font = makeUniqueNoThrow<TtfEpdFont>();
      if (!font) {
        LOG_ERR("UIFONT", "OOM: custom face");
        customOk = false;
        break;
      }
      font->addStreamSource(0, readFont, &custom, custom.size);
      const auto pt = static_cast<uint16_t>((sizes[i] * size + 50) / 100);
      if (!font->load(pt, true, 12 * 1024, 384)) {
        customOk = false;
        break;
      }
      const int id = firstId + count + i;
      renderer.insertFont(id, font->family(i == 2 || i == 3));
      renderer.registerTtfFont(id, font.get());
      slots[count + i].font = std::move(font);
    }
    if (!customOk) {
      release(renderer, count, count * 2);
      custom.close();
    }
  }
  bind(renderer);
#else
  (void)renderer;
#endif
}
}  // namespace systemUiFonts
