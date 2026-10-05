#ifdef RICKYOS_PRODUCT
#include "RickyWallpaperDownloadActivity.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NetworkStartup.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/RickyWallpaperCatalog.h"

namespace {
constexpr char kManifestTmp[] = "/wallpapers_manifest.tmp";
constexpr char kStandbyPicture[] = "/sleep.bmp";
constexpr size_t kMaxManifestBytes = 8 * 1024;
constexpr size_t kMaxItems = 24;
constexpr uint32_t kMaxItemBytes = 2 * 1024 * 1024;
constexpr size_t kMaxUrlBytes = 256;
constexpr int kFileAttempts = 3;

bool validBase(const char* url) {
  const size_t length = url ? strlen(url) : 0;
  return length > 8 && length <= kMaxUrlBytes && strncmp(url, "https://", 8) == 0 && url[length - 1] == '/';
}

// A name from the manifest becomes a card file name: plain text only, no path parts.
bool validName(const char* name) {
  const size_t length = name ? strlen(name) : 0;
  if (length == 0 || length > 64 || name[0] == '.') return false;
  return strpbrk(name, "/\\:*?\"<>|") == nullptr;
}

// ".png" or ".bmp" from the published name (pictures keep their format on the card).
std::string extensionOf(const std::string& file) {
  const size_t dot = file.rfind('.');
  return dot == std::string::npos ? std::string(".bmp") : file.substr(dot);
}

bool fileCrc32(const std::string& path, uint32_t& size, uint32_t& crc) {
  HalFile file;
  if (!Storage.openFileForRead("WPDL", path.c_str(), file)) return false;
  size = static_cast<uint32_t>(file.fileSize());
  uint8_t buffer[256];
  crc = 0;
  while (file.available()) {
    const int read = file.read(buffer, sizeof(buffer));
    if (read <= 0) break;
    crc = esp_rom_crc32_le(crc, buffer, static_cast<uint32_t>(read));
  }
  return true;
}

bool copyFile(const std::string& from, const char* to) {
  HalFile input, output;
  if (!Storage.openFileForRead("WPDL", from.c_str(), input)) return false;
  const std::string part = std::string(to) + ".part";
  if (!Storage.openFileForWrite("WPDL", part.c_str(), output)) return false;
  uint8_t buffer[512];
  bool ok = true;
  while (ok && input.available()) {
    const int read = input.read(buffer, sizeof(buffer));
    if (read <= 0) break;
    ok = output.write(buffer, static_cast<size_t>(read)) == static_cast<size_t>(read);
  }
  output.flush();
  output.close();
  input.close();
  if (!ok || !Storage.rename(part.c_str(), to)) {
    Storage.remove(part.c_str());
    return false;
  }
  return true;
}
}  // namespace

void RickyWallpaperDownloadActivity::onEnter() {
  Activity::onEnter();
  state_ = State::Connecting;
  requestUpdate();
  if (!startActivityForResultWith<WifiSelectionActivity>(
          [this](const ActivityResult& result) { onWifiResult(!result.isCancelled); })) {
    state_ = State::Failed;
    requestUpdate();
  }
}

void RickyWallpaperDownloadActivity::onExit() {
  Activity::onExit();
  const bool wifiWasEnabled = WiFi.getMode() != WIFI_MODE_NULL;
  if (wifiWasEnabled) {
    WiFi.disconnect(false);
    delay(30);
  }
  // Same as the font downloader: restart so WiFi and TLS memory is fully reclaimed.
  if (wifiWasEnabled || usedNetwork_) silentRestart();
}

void RickyWallpaperDownloadActivity::onWifiResult(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  usedNetwork_ = true;
  {
    RenderLock lock(*this);
    state_ = State::Loading;
  }
  requestUpdateAndWait();
  NetworkStartup::prepare(renderer);
  const bool ok = fetchManifest() && downloadAll();
  if (ok) installFirstIfUnset();
  {
    RenderLock lock(*this);
    state_ = ok ? State::Done : State::Failed;
  }
  requestUpdate();
}

bool RickyWallpaperDownloadActivity::fetchManifest() {
  items_.clear();
  mirrors_.clear();
  baseUrl_.clear();
  if (!HttpDownloader::hasMemoryForTls()) return false;
  // CDN caches of the "@latest" manifests go stale independently (a mainland jsDelivr
  // edge kept v1.0.0 after v1.1.0 shipped), so read every route and keep the newest
  // release, judged by the tag in its baseUrl.
  uint32_t bestRelease = 0;
  for (int attempt = 0; attempt < RickyWallpaperCatalog::MANIFEST_COUNT; ++attempt) {
    const char* url = RickyWallpaperCatalog::manifestFor(SETTINGS.contentProfile, attempt);
    if (HttpDownloader::downloadToFile(url, kManifestTmp, nullptr) != HttpDownloader::OK) {
      LOG_ERR("WPDL", "Manifest failed: %s", url);
      Storage.remove(kManifestTmp);
      continue;
    }
    JsonDocument doc;
    DeserializationError error = DeserializationError::InvalidInput;
    {
      HalFile file;
      if (Storage.openFileForRead("WPDL", kManifestTmp, file) && file.fileSize() <= kMaxManifestBytes)
        error = deserializeJson(doc, file);
    }
    Storage.remove(kManifestTmp);
    const char* base = doc["baseUrl"] | "";
    if (error || doc["version"].as<int>() != 1 || !validBase(base)) {
      LOG_ERR("WPDL", "Invalid manifest: %s", url);
      continue;
    }
    const uint32_t release = RickyWallpaperCatalog::releaseOf(base);
    LOG_INF("WPDL", "Manifest %s: release %06x", url, static_cast<unsigned>(release));
    if (!items_.empty() && release <= bestRelease) continue;
    std::vector<Item> items;
    for (JsonVariant entry : doc["items"].as<JsonArray>()) {
      const char* name = entry["name"] | "";
      const char* file = entry["file"] | "";
      const uint32_t size = entry["size"] | 0u;
      const bool picture =
          FsHelpers::hasPngExtension(std::string_view(file)) || FsHelpers::hasBmpExtension(std::string_view(file));
      if (!validName(name) || !validName(file) || !picture || size == 0 || size > kMaxItemBytes ||
          items.size() >= kMaxItems) {
        LOG_ERR("WPDL", "Skipping invalid manifest item");
        continue;
      }
      items.push_back(Item{name, file, size, entry["crc32"] | 0u});
    }
    if (items.empty()) continue;
    bestRelease = release;
    items_ = std::move(items);
    baseUrl_ = base;
    mirrors_.clear();
    for (JsonVariant mirror : doc["mirrors"].as<JsonArray>()) {
      if (validBase(mirror.as<const char*>()) && mirrors_.size() < 4) mirrors_.emplace_back(mirror.as<const char*>());
    }
  }
  // File hosts: in China the jsDelivr hosts go first whichever manifest won (GitHub is
  // slow or unreachable there); elsewhere the manifest's own order.
  hosts_.clear();
  hosts_.push_back(baseUrl_);
  hosts_.insert(hosts_.end(), mirrors_.begin(), mirrors_.end());
  if (SETTINGS.contentProfile == CrossPointSettings::ContentProfile::China) {
    std::stable_partition(hosts_.begin(), hosts_.end(),
                          [](const std::string& host) { return host.find("jsdelivr.net") != std::string::npos; });
  }
  preferredHost_ = 0;
  return !items_.empty();
}

bool RickyWallpaperDownloadActivity::downloadAll() {
  if (!Storage.ensureDirectoryExists("/images") || !Storage.ensureDirectoryExists(RickyWallpaperCatalog::FOLDER))
    return false;
  bytesTotal_ = 0;
  for (const auto& item : items_) bytesTotal_ += item.size;
  bytesDone_ = 0;
  progressBytes_ = 0;
  {
    RenderLock lock(*this);
    state_ = State::Downloading;
  }
  requestUpdate();
  for (currentItem_ = 0; currentItem_ < items_.size(); ++currentItem_) {
    const Item& item = items_[currentItem_];
    const std::string dest = std::string(RickyWallpaperCatalog::FOLDER) + "/" + item.name + extensionOf(item.file);
    if (!downloadItem(item, dest)) return false;
    bytesDone_ += item.size;
  }
  return true;
}

bool RickyWallpaperDownloadActivity::downloadItem(const Item& item, const std::string& dest) {
  uint32_t size = 0, crc = 0;
  if (Storage.exists(dest.c_str()) && fileCrc32(dest, size, crc) && size == item.size && crc == item.crc32)
    return true;  // already here
  const std::string part = dest + ".part";
  const size_t done = bytesDone_;
  const auto progress = [this, done, &item](size_t downloaded, size_t) {
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelRequested_ = true;
    progressBytes_ = done + std::min<size_t>(downloaded, item.size);
    const auto percent = static_cast<uint8_t>(progressBytes_ * 100 / std::max<size_t>(1, bytesTotal_));
    if (percent != lastPercent_) {
      lastPercent_ = percent;
      requestUpdate(true);
    }
  };
  auto result = HttpDownloader::HTTP_ERROR;
  for (int attempt = 0; attempt < kFileAttempts && result == HttpDownloader::HTTP_ERROR; ++attempt) {
    // Start on the host that served the last picture; move on after a failure.
    const size_t host = (preferredHost_ + attempt) % hosts_.size();
    if (attempt > 0) delay(2000);
    result = HttpDownloader::downloadToFile(hosts_[host] + item.file, part, progress, &cancelRequested_);
    if (result == HttpDownloader::OK) preferredHost_ = host;
  }
  if (result != HttpDownloader::OK || !fileCrc32(part, size, crc) || size != item.size || crc != item.crc32) {
    LOG_ERR("WPDL", "Download failed: %s (%d)", item.file.c_str(), result);
    Storage.remove(part.c_str());
    return false;
  }
  Storage.remove(dest.c_str());
  if (!Storage.rename(part.c_str(), dest.c_str())) return false;
  // An earlier release shipped this picture as a BMP; keep one copy in the folder.
  const std::string older = std::string(RickyWallpaperCatalog::FOLDER) + "/" + item.name + ".bmp";
  if (older != dest && Storage.exists(older.c_str())) Storage.remove(older.c_str());
  return true;
}

void RickyWallpaperDownloadActivity::installFirstIfUnset() {
  // Never replace a picture the user chose.
  if (items_.empty() || Storage.exists(kStandbyPicture)) return;
  const std::string first =
      std::string(RickyWallpaperCatalog::FOLDER) + "/" + items_.front().name + extensionOf(items_.front().file);
  bool installed = false;
  if (FsHelpers::hasPngExtension(first)) {
    // Same conversion the picture preview uses: an 8-bit gray BMP keeps all 16 grays.
    GfxRenderer::FrameBufferLoan loan(renderer);
    installed = PngToBmpConverter::pngFileToGray8BmpFile(first.c_str(), kStandbyPicture, true);
  } else {
    installed = copyFile(first, kStandbyPicture);
  }
  if (installed) {
    SETTINGS.sleepScreen = CrossPointSettings::CUSTOM;
    SETTINGS.saveToFile();
  } else {
    LOG_ERR("WPDL", "Could not install %s", first.c_str());
  }
}

void RickyWallpaperDownloadActivity::loop() {
  if (state_ != State::Done && state_ != State::Failed) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) finish();
}

void RickyWallpaperDownloadActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const Rect page{0, 0, width, height};
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int centerY = height / 2;
  UITheme::drawCenteredText(renderer, page, UI_12_FONT_ID, centerY - lineHeight * 3, tr(STR_RICKY_WALLPAPER_DOWNLOAD));
  char status[64];
  switch (state_) {
    case State::Connecting:
    case State::Loading:
      snprintf(status, sizeof(status), "%s", tr(STR_RICKY_WALLPAPER_LOADING));
      break;
    case State::Downloading:
      snprintf(status, sizeof(status), tr(STR_RICKY_WALLPAPER_PROGRESS),
               static_cast<unsigned>(std::min(currentItem_ + 1, items_.size())), static_cast<unsigned>(items_.size()));
      break;
    case State::Done:
      snprintf(status, sizeof(status), tr(STR_RICKY_WALLPAPER_DONE), static_cast<unsigned>(items_.size()));
      break;
    case State::Failed:
      snprintf(status, sizeof(status), "%s", tr(STR_RICKY_WALLPAPER_FAILED));
      break;
  }
  UITheme::drawCenteredText(renderer, page, UI_10_FONT_ID, centerY - lineHeight, status);
  if (state_ == State::Downloading) {
    const int barWidth = width * 2 / 3;
    GUI.drawProgressBar(renderer, Rect{(width - barWidth) / 2, centerY + lineHeight, barWidth, 16}, progressBytes_,
                        std::max<size_t>(1, bytesTotal_));
  }
  if (state_ == State::Done || state_ == State::Failed) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
#endif
