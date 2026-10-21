#include "BookFontSet.h"

#include <Epub.h>
#include <Epub/css/CssParser.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <VectorFontSupport.h>

#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"

#if CROSSPOINT_VECTOR_FONTS
#include <TtfEpdFont.h>
#endif

namespace {

bool equalsCaseless(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
    const char y = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
    if (x != y) return false;
  }
  return true;
}

std::string_view trimmed(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

// Same scheme as the reader TTF ids (FNV-1a), salted apart from them.
int bookFontId(const std::string& path, const uint8_t pointSize) {
  uint32_t hash = 2166136261u;
  for (const char c : path) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  hash ^= pointSize;
  hash *= 16777619u;
  hash ^= 0x424F4F4Bu;  // "BOOK"
  const int id = static_cast<int>(hash);
  return id != 0 ? id : 1;
}

}  // namespace

BookFontSet::Face::Face() = default;
BookFontSet::Face::~Face() = default;  // the owner frees bytes in unload()
BookFontSet::Face::Face(Face&&) noexcept = default;
BookFontSet::Face& BookFontSet::Face::operator=(Face&&) noexcept = default;

BookFontSet::~BookFontSet() {
  if (bookfonts::active() == this) bookfonts::setActive(nullptr);
  for (auto& face : faces_) unload(face);
}

void BookFontSet::open(const Epub& epub) { epub_ = &epub; }

void BookFontSet::collectFaces() {
  // Epub keeps the CSS rules only while a chapter is laid out (they are released after
  // load and reloaded from cache by createSectionFile), so the faces are read here, on
  // the first font-family the layout asks about, and kept for the rest of the session.
  if (collected_ || !epub_) return;
  const CssParser* css = epub_->getCssParser();
  if (!css || css->empty()) return;
  collected_ = true;
  for (uint8_t i = 0; i < css->fontFaceCount() && faces_.size() < kMaxFaces; ++i) {
    const CssParser::FontFace declared = css->fontFaceAt(i);
    if (declared.path.empty()) continue;  // a system-font name only (宋体 / 楷体 …)
    Face face;
    face.family.assign(declared.family.data(), declared.family.size());
    face.path.assign(declared.path.data(), declared.path.size());
    faces_.push_back(std::move(face));
  }
  if (!faces_.empty()) LOG_INF("BOOKFONT", "%u book faces declared", static_cast<unsigned>(faces_.size()));
}

uint8_t BookFontSet::faceForFamily(const std::string_view candidates) {
  collectFaces();
  size_t start = 0;
  while (start <= candidates.size()) {
    size_t end = candidates.find(',', start);
    if (end == std::string_view::npos) end = candidates.size();
    const std::string_view name = trimmed(candidates.substr(start, end - start));
    if (!name.empty()) {
      for (size_t i = 0; i < faces_.size(); ++i) {
        if (equalsCaseless(faces_[i].family, name)) return faces_[i].failed ? 0 : static_cast<uint8_t>(i + 1);
      }
    }
    start = end + 1;
  }
  return 0;
}

int BookFontSet::fontIdFor(const uint8_t face, const int baseFontId, uint16_t /*size*/) {
#if CROSSPOINT_VECTOR_FONTS
  if (face == 0 || face > faces_.size()) return baseFontId;
  Face& entry = faces_[face - 1];
  if (entry.failed) return baseFontId;
  const uint8_t pointSize = SETTINGS.fontPointSize;
  if (entry.font && entry.pointSize == pointSize) return entry.fontId;
  if (entry.font) releaseFont(entry);  // the reader size changed; keep the bytes
  return load(entry, pointSize, baseFontId) ? entry.fontId : baseFontId;
#else
  (void)face;
  return baseFontId;
#endif
}

bool BookFontSet::load(Face& face, const uint8_t pointSize, const int baseFontId) {
#if CROSSPOINT_VECTOR_FONTS
  if (!epub_) return false;
  if (!face.bytes) {
    size_t length = 0;
    if (!epub_->getItemSize(face.path, &length) || length < 64 || length > kMaxFaceBytes) {
      LOG_ERR("BOOKFONT", "Face %s: %s missing or unusable (%u B)", face.family.c_str(), face.path.c_str(),
              static_cast<unsigned>(length));
      face.failed = true;
      return false;
    }
    if (HalMemory::getPsramHeap().freeBytes < length + kPsramReserve) {
      // Not marked failed: PSRAM may be free again on a later chapter.
      LOG_INF("BOOKFONT", "Face %s (%u KB) waits: PSRAM short", face.family.c_str(),
              static_cast<unsigned>(length / 1024));
      return false;
    }
    face.bytes = epub_->readItemContentsToBytes(face.path, &face.length);
    if (!face.bytes) {
      face.failed = true;
      return false;
    }
  }
  auto font = makeUniqueNoThrow<TtfEpdFont>();
  if (!font) return false;
  font->addResidentSource(TtfEpdFont::Regular, face.bytes, static_cast<uint32_t>(face.length));
  const bool fourBit = renderer_.getGrayscaleLevels() == 16;
  // A decorative or heading face draws a few hundred distinct glyphs at most.
  if (!font->load(pointSize, /*twoBit=*/true, /*glyphCacheBytes=*/192 * 1024, /*maxGlyphs=*/1024, fourBit)) {
    LOG_ERR("BOOKFONT", "Face %s: %s is not a usable font", face.family.c_str(), face.path.c_str());
    free(face.bytes);
    face.bytes = nullptr;
    face.length = 0;
    face.failed = true;
    return false;
  }
  font->build(" ");
  face.fontId = bookFontId(face.path, pointSize);
  renderer_.insertFont(face.fontId, font->family());
  renderer_.registerTtfFont(face.fontId, font.get());
  renderer_.setFallbackFont(face.fontId, baseFontId);
  face.font = std::move(font);
  face.pointSize = pointSize;
  LOG_INF("BOOKFONT", "Face %s loaded from %s (%u KB) at %upt", face.family.c_str(), face.path.c_str(),
          static_cast<unsigned>(face.length / 1024), pointSize);
  return true;
#else
  (void)face;
  (void)pointSize;
  (void)baseFontId;
  return false;
#endif
}

void BookFontSet::releaseFont(Face& face) {
#if CROSSPOINT_VECTOR_FONTS
  if (face.font) {
    renderer_.unregisterTtfFont(face.fontId);
    renderer_.removeFont(face.fontId);
    face.font.reset();
  }
#endif
  face.fontId = 0;
  face.pointSize = 0;
}

void BookFontSet::unload(Face& face) {
  releaseFont(face);
  if (face.bytes) {
    free(face.bytes);
    face.bytes = nullptr;
    face.length = 0;
  }
}
