#include "HalCamera.h"

#include "MetalioFeatures.h"
#if RICKYOS_METALIO_CAMERA_ENABLED
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MetalioCamera.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace HalCamera {
namespace {
Snapshot current;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> working{false}, restart{false}, persistPending{false}, leavePending{false};
char directory[96]{};
UsbUvcStill::JpegBuffer preview;
enum class Job { Preview, Save, CaptureAndSave };
Job job = Job::Preview;
bool usbBusy() { return Storage.usbDriveState() != UsbDriveState::Unsupported; }
bool savePhoto(Snapshot& result) {
  if (preview.empty() || !Storage.ensureDirectoryExists(directory)) return false;
  char path[sizeof(result.path)]{};
  bool unique = false;
  for (unsigned i = 0; i < 100; ++i) {
    snprintf(path, sizeof(path), "%s/CAM_%llu_%02u.jpg", directory, static_cast<unsigned long long>(time(nullptr)), i);
    if (!Storage.exists(path)) {
      unique = true;
      break;
    }
  }
  if (!unique) return false;
  bool saved = false;
  {
    auto file = Storage.open(path, O_WRONLY | O_CREAT | O_EXCL);
    if (file) {
      size_t offset = 0;
      while (offset < preview.len) {
        const size_t count = std::min<size_t>(16384, preview.len - offset);
        if (file.write(preview.data + offset, count) != count) break;
        offset += count;
      }
      file.flush();
      saved = offset == preview.len && file.fileSize() == preview.len;
    }
  }
  if (!saved) {
    Storage.remove(path);
    return false;
  }
  snprintf(result.path, sizeof(result.path), "%s", path);
  result.bytes = preview.len;
  return true;
}
void worker(void*) {
  {
    // Scope must finish before vTaskDeleteWithCaps, which cannot unwind C++ locals.
    HalPowerManager::Lock power;
    Snapshot result = snapshot();
    result.error = ESP_OK;
    if (job != Job::Save) {
      UsbUvcStill::JpegBuffer jpeg;
      UsbUvcStill::DetectInfo info;
      result.error = freeink::metalio_camera::capture(jpeg, info);
      const auto diag = freeink::metalio_camera::diagnostic();
      result.phase = diag.phase;
      result.configDescriptorBytes = diag.configDescriptorBytes;
      memcpy(result.powerBefore, diag.powerBefore, 5);
      memcpy(result.powerAfter, diag.powerAfter, 5);
      snprintf(result.usbError, sizeof(result.usbError), "%s", diag.usbError);
      result.hostPort = diag.hostPort;
      result.hostInterrupt = diag.hostInterrupt;
      result.phyRoute = diag.phyRoute;
      result.usbSerial = diag.usbSerial;
      result.muxOutput = diag.muxOutput;
      memcpy(result.powerLate, diag.powerLate, 5);
      result.internalBefore = diag.internalBefore;
      result.internalAfter = diag.internalAfter;
      result.largestBefore = diag.largestBefore;
      result.largestAfter = diag.largestAfter;
      if (result.error == ESP_OK && !jpeg.empty()) {
        preview.Release();
        preview = jpeg;
        jpeg.data = nullptr;
        jpeg.len = 0;
        result.width = preview.width;
        result.height = preview.height;
        ++result.generation;
        result.frameReady = true;
      } else if (result.error == ESP_OK)
        result.error = ESP_FAIL;
      jpeg.Release();
    }
    if (result.error == ESP_OK && job != Job::Preview && !savePhoto(result)) result.error = ESP_FAIL;
    result.state = result.error != ESP_OK ? State::Error : (job == Job::Preview ? State::Preview : State::Saved);
    portENTER_CRITICAL(&stateMux);
    current = result;
    portEXIT_CRITICAL(&stateMux);
    // Never write NVS from this PSRAM-stack task: flash disables caches. The
    // main loop commits the bounded result before any diagnostic reboot.
    if (job != Job::Preview || result.error != ESP_OK) persistPending.store(true);
    LOG_INF("CAM", "job=%u state=%u phase=%lu error=%d generation=%lu bytes=%lu size=%ux%u file=%s",
            static_cast<unsigned>(job), static_cast<unsigned>(result.state), static_cast<unsigned long>(result.phase),
            result.error, static_cast<unsigned long>(result.generation), static_cast<unsigned long>(result.bytes),
            result.width, result.height, result.path);
  }
  working.store(false);
  vTaskDeleteWithCaps(nullptr);
}
bool launch(Job next) {
  bool idle = false;
  if (!working.compare_exchange_strong(idle, true)) return false;
  job = next;
  portENTER_CRITICAL(&stateMux);
  current.state = State::Working;
  portEXIT_CRITICAL(&stateMux);
  // JPEG/control stack belongs in PSRAM; no Flash/NVS operation occurs here.
  if (xTaskCreatePinnedToCoreWithCaps(worker, "rick_camera", 12288, nullptr, 4, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    portENTER_CRITICAL(&stateMux);
    current.state = State::Error;
    current.error = ESP_ERR_NO_MEM;
    portEXIT_CRITICAL(&stateMux);
    working.store(false);
    return false;
  }
  return true;
}
}  // namespace
void recoverUsbBeforeSerial() { freeink::metalio_camera::recoverSerialRoute(); }
void begin(const char* photoDirectory) {
  snprintf(directory, sizeof(directory), "%s", photoDirectory);
  Preferences prefs;
  if (prefs.begin("rickcam", true)) {
    prefs.getString("last", current.path, sizeof(current.path));
    current.error = prefs.getInt("error", 0);
    current.bytes = prefs.getUInt("bytes", 0);
    current.width = prefs.getUInt("width", 0);
    current.height = prefs.getUInt("height", 0);
    if (current.error)
      current.state = State::Error;
    else if (current.path[0])
      current.state = State::Saved;
    // Versioned diagnostic record survives a full power cycle. Read it only
    // when the exact private layout version and byte count match this build.
    if (prefs.getUInt("diagver", 0) == 1 && prefs.getBytesLength("diag") == sizeof(Snapshot)) {
      Snapshot stored;
      if (prefs.getBytes("diag", &stored, sizeof(stored)) == sizeof(stored)) {
        stored.path[sizeof(stored.path) - 1] = 0;
        stored.usbError[sizeof(stored.usbError) - 1] = 0;
        stored.frameReady = false;
        stored.generation = 0;
        current = stored;
      }
    }
    prefs.end();
  }
  const auto diag = freeink::metalio_camera::diagnostic();
  if (diag.phase) {
    current.phase = diag.phase;
    current.configDescriptorBytes = diag.configDescriptorBytes;
    memcpy(current.powerBefore, diag.powerBefore, 5);
    memcpy(current.powerAfter, diag.powerAfter, 5);
    snprintf(current.usbError, sizeof(current.usbError), "%s", diag.usbError);
    current.hostPort = diag.hostPort;
    current.hostInterrupt = diag.hostInterrupt;
    current.phyRoute = diag.phyRoute;
    current.usbSerial = diag.usbSerial;
    current.muxOutput = diag.muxOutput;
    memcpy(current.powerLate, diag.powerLate, 5);
    current.internalBefore = diag.internalBefore;
    current.internalAfter = diag.internalAfter;
    current.largestBefore = diag.largestBefore;
    current.largestAfter = diag.largestAfter;
  }
  freeink::metalio_camera::setUsbBusyProbe(usbBusy);
}
void pump() {
  if (busy()) return;
  if (leavePending.exchange(false)) {
    preview.Release();
    portENTER_CRITICAL(&stateMux);
    current.frameReady = false;
    portEXIT_CRITICAL(&stateMux);
    if (freeink::metalio_camera::needsRestart()) restart.store(true);
  }
  if (!persistPending.exchange(false)) return;
  const auto result = snapshot();
  Preferences prefs;
  if (!prefs.begin("rickcam", false)) {
    persistPending.store(true);
    return;
  }
  if (result.path[0]) prefs.putString("last", result.path);
  prefs.putBytes("diag", &result, sizeof(result));
  prefs.putUInt("diagver", 1);
  prefs.putInt("error", result.error);
  prefs.putUInt("bytes", result.bytes);
  prefs.putUInt("width", result.width);
  prefs.putUInt("height", result.height);
  prefs.end();
}
bool capture() { return launch(Job::CaptureAndSave); }
bool requestPreview() { return launch(Job::Preview); }
bool saveCurrent() { return !busy() && !preview.empty() && launch(Job::Save); }
Frame frame() { return busy() ? Frame{} : Frame{preview.data, preview.len, preview.width, preview.height}; }
bool busy() { return working.load(); }
bool usbOwned() { return busy() || freeink::metalio_camera::needsRestart(); }
Snapshot snapshot() {
  portENTER_CRITICAL(&stateMux);
  Snapshot copy = current;
  portEXIT_CRITICAL(&stateMux);
  return copy;
}
bool prepareForSleep() {
  const uint32_t started = millis();
  while (busy() && millis() - started < 45000) delay(20);
  if (busy()) return false;
  pump();
  return true;
}
void leave() {
  leavePending.store(true);
  prepareForSleep();
}
bool consumeRestartRequest() { return restart.exchange(false); }
}  // namespace HalCamera
#else
namespace HalCamera {
void recoverUsbBeforeSerial() {}
void begin(const char*) {}
void pump() {}
bool capture() { return false; }
bool requestPreview() { return false; }
bool saveCurrent() { return false; }
Frame frame() { return {}; }
bool busy() { return false; }
bool usbOwned() { return false; }
Snapshot snapshot() { return {}; }
void leave() {}
bool consumeRestartRequest() { return false; }
bool prepareForSleep() { return true; }
}  // namespace HalCamera
#endif
