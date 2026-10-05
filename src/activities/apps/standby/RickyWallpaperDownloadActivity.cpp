#ifdef RICKYOS_PRODUCT
#include "RickyWallpaperDownloadActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

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
constexpr size_t kMaxItems = 16;
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
  auto result = HttpDownloader::HTTP_ERROR;
  for (int attempt = 0; attempt < RickyWallpaperCatalog::MANIFEST_COUNT && result != HttpDownloader::OK; ++attempt) {
    const char* url = RickyWallpaperCatalog::manifestFor(SETTINGS.contentProfile, attempt);
    result = HttpDownloader::downloadToFile(url, kManifestTmp, nullptr);
    if (result != HttpDownloader::OK) {
      LOG_ERR("WPDL", "Manifest failed: %s", url);
      Storage.remove(kManifestTmp);
    }
  }
  if (result != HttpDownloader::OK) return false;

  JsonDocument doc;
  DeserializationError error = DeserializationError::InvalidInput;
  {
    HalFile file;
    if (Storage.openFileForRead("WPDL", kManifestTmp, file) && file.fileSize() <= kMaxManifestBytes)
      error = deserializeJson(doc, file);
  }
  Storage.remove(kManifestTmp);
  if (error || doc["version"].as<int>() != 1 || !validBase(doc["baseUrl"] | "")) {
    LOG_ERR("WPDL", "Invalid manifest");
    return false;
  }
  baseUrl_ = doc["baseUrl"].as<const char*>();
  for (JsonVariant mirror : doc["mirrors"].as<JsonArray>()) {
    if (validBase(mirror.as<const char*>()) && mirrors_.size() < 4) mirrors_.emplace_back(mirror.as<const char*>());
  }
  for (JsonVariant entry : doc["items"].as<JsonArray>()) {
    const char* name = entry["name"] | "";
    const char* file = entry["file"] | "";
    const uint32_t size = entry["size"] | 0u;
    if (!validName(name) || !validName(file) || size == 0 || size > kMaxItemBytes || items_.size() >= kMaxItems) {
      LOG_ERR("WPDL", "Skipping invalid manifest item");
      continue;
    }
    items_.push_back(Item{name, file, size, entry["crc32"] | 0u});
  }
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
    const std::string dest = std::string(RickyWallpaperCatalog::FOLDER) + "/" + item.name + ".bmp";
    if (!downloadItem(item, dest)) return false;
    bytesDone_ += item.size;
  }
  return true;
}

bool RickyWallpaperDownloadActivity::downloadItem(const Item& item, const std::string& dest) {
  uint32_t size = 0, crc = 0;
  if (fileCrc32(dest, size, crc) && size == item.size && crc == item.crc32) return true;  // already here
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
  const size_t hosts = 1 + mirrors_.size();
  auto result = HttpDownloader::HTTP_ERROR;
  for (int attempt = 0; attempt < kFileAttempts && result == HttpDownloader::HTTP_ERROR; ++attempt) {
    const std::string& base = attempt % hosts == 0 ? baseUrl_ : mirrors_[attempt % hosts - 1];
    if (attempt > 0) delay(2000);
    result = HttpDownloader::downloadToFile(base + item.file, part, progress, &cancelRequested_);
  }
  if (result != HttpDownloader::OK || !fileCrc32(part, size, crc) || size != item.size || crc != item.crc32) {
    LOG_ERR("WPDL", "Download failed: %s (%d)", item.file.c_str(), result);
    Storage.remove(part.c_str());
    return false;
  }
  Storage.remove(dest.c_str());
  return Storage.rename(part.c_str(), dest.c_str());
}

void RickyWallpaperDownloadActivity::installFirstIfUnset() {
  // Never replace a picture the user chose.
  if (items_.empty() || Storage.exists(kStandbyPicture)) return;
  const std::string first = std::string(RickyWallpaperCatalog::FOLDER) + "/" + items_.front().name + ".bmp";
  if (!copyFile(first, kStandbyPicture)) LOG_ERR("WPDL", "Could not install %s", first.c_str());
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
