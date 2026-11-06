#include "BookCoverLoader.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#if defined(RICKYOS_KINDLE_IMPORT)
#include <JpegToBmpConverter.h>
#include <KindleImport.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#endif

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include "PanelTileCache.h"

namespace BookCoverLoader {
namespace {

enum class CachedCoverState { Missing, Ready, Terminal };

bool supportsCover(const std::string& path) {
#if defined(RICKYOS_KINDLE_IMPORT)
  if (kindle::supported(path)) return true;
#endif
  return FsHelpers::hasEpubExtension(path);
}

// Thumbnails known to have no usable cover in this session, so a page that returns
// does not reopen their markers. Bounded; cleared by forgetDrawnCovers().
constexpr size_t kMaxKnownCoverless = 64;
std::vector<std::string> knownCoverless;
std::mutex knownCoverlessLock;

bool isKnownCoverless(const std::string& path) {
  const std::lock_guard<std::mutex> guard(knownCoverlessLock);
  return std::find(knownCoverless.begin(), knownCoverless.end(), path) != knownCoverless.end();
}

void rememberCoverless(const std::string& path) {
  const std::lock_guard<std::mutex> guard(knownCoverlessLock);
  if (std::find(knownCoverless.begin(), knownCoverless.end(), path) != knownCoverless.end()) return;
  if (knownCoverless.size() >= kMaxKnownCoverless) knownCoverless.erase(knownCoverless.begin());
  knownCoverless.push_back(path);
}

CachedCoverState inspectCachedCover(const std::string& path, const bool emptyIsTerminal,
                                    const bool thumbnail = false) {
  if (!Storage.exists(path.c_str())) return CachedCoverState::Missing;

  {
    HalFile file;
    if (!Storage.openFileForRead("COVER", path, file)) return CachedCoverState::Terminal;
    if (file.fileSize() == 0 && emptyIsTerminal) {
#if RICKYOS_WEBP
      // A legacy marker may mean "WebP decoder unavailable". Retry it once
      // after this capability upgrade, then retain normal coverless caching.
      if (Storage.exists((path + ".webp-v1").c_str())) return CachedCoverState::Terminal;
#else
      return CachedCoverState::Terminal;
#endif
    }

    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0 &&
        !(thumbnail && isOutdatedThumbnail(bitmap))) {
      return CachedCoverState::Ready;
    }
  }

  PanelTileCache::forget(path);
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("COVER", "Failed to remove invalid cover: %s", path.c_str());
    return CachedCoverState::Terminal;
  }
  return CachedCoverState::Missing;
}

template <typename Generate>
std::string ensureCachedCover(const std::string& path, const bool emptyIsTerminal, bool* generated,
                              Generate&& generate, const bool thumbnail = false) {
  if (generated) *generated = false;
  const CachedCoverState state = inspectCachedCover(path, emptyIsTerminal, thumbnail);
  if (state == CachedCoverState::Ready) return path;
  if (state == CachedCoverState::Terminal) return "";
  if (!generate()) {
    // Books without a usable cover would otherwise reload their metadata on every
    // request. An empty file is the terminal marker inspectCachedCover() honours;
    // clearing the reading cache removes it.
    if (emptyIsTerminal && !Storage.exists(path.c_str())) {
      HalFile marker;
      Storage.openFileForWrite("COVER", path, marker);
    }
#if RICKYOS_WEBP
    if (emptyIsTerminal && Storage.exists(path.c_str())) {
      HalFile capability;
      if (Storage.openFileForWrite("COVER", path + ".webp-v1", capability)) {
        const uint8_t version=1;
        capability.write(&version,1);
      }
    }
#endif
    return "";
  }
  PanelTileCache::forget(path);
  if (inspectCachedCover(path, emptyIsTerminal, thumbnail) != CachedCoverState::Ready) return "";
  if (generated) *generated = true;
  return path;
}

#if defined(RICKYOS_KINDLE_IMPORT)
std::string ensureKindleCover(const std::string& bookPath, const int height, bool* generated) {
  Epub epub(bookPath, "/.crosspoint");
  const std::string path = height > 0 ? epub.getThumbBmpPath(height) : epub.getCoverBmpPath();
  if (isKnownCoverless(path)) return "";
  bool noCover = false;
  // Transient parser/I/O/OOM failures must not become permanent empty markers.
  std::string result = ensureCachedCover(
      path, false, generated,
      [&]() {
        epub.setupCacheDir();
        const std::string temporary = epub.getCachePath() + "/.kindle-cover.tmp";
        const ScopedCleanup cleanup{[&] { Storage.remove(temporary.c_str()); }};
        std::string sourcePath = epub.getCoverOverridePath();
        if (!epub.hasCoverOverride()) {
          kindle::Error error;
          if (!kindle::extractCover(bookPath, temporary, error)) {
            noCover = error == kindle::Error::None;
            if (!noCover) LOG_ERR("COVER", "Kindle cover read failed: %d", static_cast<int>(error));
            return false;
          }
          sourcePath = temporary;
        }
        bool converted = false;
        {
          HalFile source, output;
          uint8_t magic[8] = {};
          if (!Storage.openFileForRead("COVER", sourcePath, source) ||
              source.read(magic, sizeof(magic)) != static_cast<int>(sizeof(magic)) || !source.seek(0) ||
              !Storage.openFileForWrite("COVER", path, output))
            return false;
          const bool jpeg = magic[0] == 0xff && magic[1] == 0xd8 && magic[2] == 0xff;
          const bool png = memcmp(magic, "\x89PNG\r\n\x1a\n", sizeof(magic)) == 0;
          if (height > 0) {
            const int width = height * 3 / 5;
            if (jpeg)
              converted = JpegToBmpConverter::jpegFileToThumbBmpStreamWithSize(source, output, width, height);
            else if (png)
              converted = PngToBmpConverter::pngFileToThumbBmpStreamWithSize(source, output, width, height);
          } else {
            if (jpeg)
              converted = JpegToBmpConverter::jpegFileToCoverBmpStream(source, output, false, false);
            else if (png)
              converted = PngToBmpConverter::pngFileToCoverBmpStream(source, output, false, false);
          }
        }
        if (!converted) Storage.remove(path.c_str());
        return converted;
      },
      height > 0);
  if (noCover) rememberCoverless(path);
  return result;
}
#endif

}  // namespace

static std::string ensureThumbnailImpl(const std::string& bookPath, const int height, bool* generated,
                                       bool background) {
  if (generated) *generated = false;
  if (height <= 0) return "";
  if (const std::string known = thumbnailPath(bookPath, height); !known.empty() && isKnownCoverless(known)) return "";
  if (!Storage.exists(bookPath.c_str())) return "";

#if defined(RICKYOS_KINDLE_IMPORT)
  if (kindle::supported(bookPath)) return ensureKindleCover(bookPath, height, generated);
#endif

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    const std::string path = epub.getThumbBmpPath(height);
    std::string result = ensureCachedCover(path, !epub.hasCoverOverride(), generated, [&]() {
      // The shelf needs only the cover declaration, not a reading cache with
      // the whole spine, TOC, and styles.
      if (background) return epub.generateThumbBmpFromSource(height);
      // A book that declares no cover would otherwise pay a full load (seconds on a
      // large folder) only to find that out. The terminal marker then remembers it.
      bool declared = true;
      if (!epub.hasCoverOverride() && epub.probeCover(declared) && !declared) return false;
      if (!epub.load(true, true)) return false;
      epub.setupCacheDir();
      return epub.generateThumbBmp(height);
    }, /*thumbnail=*/true);
    if (result.empty()) rememberCoverless(path);
    return result;
  }


  return "";
}

std::string ensureThumbnail(const std::string& bookPath, const int height, bool* generated) {
  return ensureThumbnailImpl(bookPath, height, generated, false);
}

bool ThumbnailJob::start(const std::string& book, int height) {
  if (running() || book.empty()) return false;
  book_ = book;
  height_ = height;
  made_ = false;
  running_.store(true, std::memory_order_release);
  // Same stack and low priority as the library import worker. Allocate it
  // only for the current thumbnail; failure leaves the title-only card.
  if (xTaskCreate(&worker, "BookCover", 8192, this, 1, nullptr) == 1) return true;  // pdPASS
  running_.store(false, std::memory_order_release);
  LOG_ERR("COVER", "Unable to start thumbnail task");
  return false;
}

bool ThumbnailJob::isWriting(const std::string& book, int height) const {
  return running() && height_ == height && book_ == book;
}

void ThumbnailJob::wait() const {
  while (running()) delay(1);
}

void ThumbnailJob::worker(void* context) {
  auto* job = static_cast<ThumbnailJob*>(context);
  const auto started = millis();
  job->made_ = !ensureThumbnailImpl(job->book_, job->height_, nullptr, true).empty();
  LOG_INF("COVER", "background thumbnail %s in %lu ms", job->made_ ? "ready" : "unavailable", millis() - started);
  job->running_.store(false, std::memory_order_release);
  vTaskDelete(nullptr);  // no job access after publication
}

std::string ensureFullCover(const std::string& bookPath, std::string* title, std::string* author, bool* generated) {
  if (title) title->clear();
  if (author) author->clear();
  if (generated) *generated = false;
  if (!Storage.exists(bookPath.c_str())) return "";

#if defined(RICKYOS_KINDLE_IMPORT)
  if (kindle::supported(bookPath)) return ensureKindleCover(bookPath, 0, generated);
#endif

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    const std::string path = epub.getCoverBmpPath();
    return ensureCachedCover(path, false, generated, [&]() {
      if (!epub.load(true, true)) return false;
      epub.setupCacheDir();
      if (title) *title = epub.getTitle();
      if (author) *author = epub.getAuthor();
      return epub.generateCoverBmp();
    });
  }


  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    const std::string path = txt.getCoverBmpPath();
    return ensureCachedCover(path, false, generated, [&]() {
      if (!txt.load()) return false;
      txt.setupCacheDir();
      if (title) *title = txt.getTitle();
      return txt.generateCoverBmp();
    });
  }

  return "";
}

std::string thumbnailPath(const std::string& bookPath, const int height) {
  if (height <= 0 || !supportsCover(bookPath)) return "";
  return Epub(bookPath, "/.crosspoint").getThumbBmpPath(height);
}

bool isOutdatedThumbnail(const Bitmap& bitmap) {
#if FREEINK_DEVICE_READPICO || FREEINK_DEVICE_METALIO_PLUS || FREEINK_DEVICE_METALIO_EINK4 || defined(SIMULATOR_DEVICE_READPICO)
  return bitmap.is1Bit();
#else
  (void)bitmap;
  return false;
#endif
}

bool canMakeThumbnail(const std::string& bookPath, const int height) {
  const std::string path = thumbnailPath(bookPath, height);
  return !path.empty() && !isKnownCoverless(path);
}

void forgetDrawnCovers() {
  {
    const std::lock_guard<std::mutex> guard(knownCoverlessLock);
    knownCoverless.clear();
  }
  PanelTileCache::clear();
}

}  // namespace BookCoverLoader
