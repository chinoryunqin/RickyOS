#include <Arduino.h>
#include <Bitmap.h>
#include <BuildScratch.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <JpegToBmpConverter.h>
#include <PNGdec.h>
#include <SdCardFont.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <optional>
#include <vector>

#include "AirPageImageRenderer.h"
#include "AirPageWallpaper.h"
#include "CrossPointSettings.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/converters/JpegToFramebufferConverter.h"
#include "Epub/converters/PngToFramebufferConverter.h"
#include "QrParams.h"
#include "SleepProbe.h"
#include "components/themes/BaseTheme.h"

// Native rendering bypasses ImageBlock; the four-level seam checks error forwarding.
ImageBlock::ImageBlock(const std::string&, const std::string&, int16_t, int16_t) : width(0), height(0) {}
void ImageBlock::releaseRenderCache() {}
void ImageBlock::clearSessionRenderFailures() {}
static ImageRenderError legacyError = ImageRenderError::Failed;
bool ImageBlock::render(GfxRenderer&, int, int, PixelCachePolicy, ImageRenderError* error) {
  if (error) *error = legacyError;
  return false;
}
static HalMemory::HeapStats availableHeap{6236320, 8373520, 0, 4980724};
HalMemory::HeapStats HalMemory::getDefaultHeap() { return availableHeap; }
HalMemory::HeapStats HalMemory::getInternalHeap() { return {33087, 283859, 9796, 11252}; }
HalMemory::HeapStats HalMemory::getPsramHeap() { return {6235816, 8373520, 4112312, 4980724}; }
static bool failDecoderAllocation = false;
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  return (size == sizeof(JPEGDEC) || size == sizeof(PNG)) && failDecoderAllocation ? nullptr : std::malloc(size);
}

bool FontCacheManager::isScanning() const { return false; }
void FontCacheManager::clearCache() {}
void SdCardFont::clearCache() {}
ESPMock ESP;
namespace {
std::array<uint8_t, HalDisplay::BUFFER_SIZE> bw;
std::array<uint8_t, HalDisplay::DISPLAY_WIDTH * HalDisplay::DISPLAY_HEIGHT / 2> native;
auto visibleNative = native;
auto visibleBw = bw;
uint8_t levels = 16;
bool loan = false, failRefresh = false, failAllocation = false;
int commits = 0, cancels = 0;
std::array<int, 6> uiRegion{};
std::vector<char> refreshEvents;
}  // namespace
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  return failAllocation ? nullptr : std::malloc(size);
}
HalDisplay::HalDisplay() {}
HalDisplay::~HalDisplay() {}
uint8_t* HalDisplay::getFrameBuffer() const { return bw.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::clearScreen(uint8_t value) const { bw.fill(value); }
uint8_t HalDisplay::getGrayscaleLevels() const { return levels; }
void HalDisplay::setNextGray16UiRefresh(int x, int y, int w, int h, uint8_t content, uint8_t small) {
  uiRegion = {x, y, w, h, content, small};
}
uint8_t* HalDisplay::beginGrayscale16() {
  if (loan) return nullptr;
  loan = true;
  refreshEvents.push_back('B');
  native.fill(0xFF);
  return native.data();
}
bool HalDisplay::commitGrayscale16() {
  loan = false;
  ++commits;
  refreshEvents.push_back('C');
  if (!failRefresh) {
    visibleNative = native;
    visibleBw = bw;
  }
  return !failRefresh;
}
uint8_t* HalDisplay::beginRetainedGrayscale16() {
  if (loan) return nullptr;
  loan = true;
  native = visibleNative;
  bw = visibleBw;
  return native.data();
}
void HalDisplay::cancelGrayscale16() {
  loan = false;
  ++cancels;
  refreshEvents.push_back('X');
}
bool HalDisplay::isInverted() const { return false; }
void HalDisplay::displayBuffer(RefreshMode mode, bool) {
  assert(!loan);
  switch (mode) {
    case FULL_REFRESH:
      for (const auto byte : bw) assert(byte == 0xFF);
      refreshEvents.push_back('F');
      break;
    case HALF_REFRESH:
      refreshEvents.push_back('H');
      break;
    case FAST_REFRESH:
      // Record white fast clears separately from the following image refresh.
      refreshEvents.push_back(std::all_of(bw.begin(), bw.end(), [](uint8_t byte) { return byte == 0xFF; }) ? 'w' : 'f');
      break;
  }
}
void HalDisplay::displayGrayscaleBase(RefreshMode, bool) {}
bool HalDisplay::displayGrayscaleBase(GrayscaleMode, RefreshMode mode, bool off) {
  displayGrayscaleBase(mode, off);
  return true;
}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool, const unsigned char*, bool) { refreshEvents.push_back('G'); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
HalDisplay::Controller HalDisplay::getController() const { return Controller::LgfxEpd; }
HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode) const { return {}; }
HalDisplay display;

namespace {
uint8_t tone(int x, int y) {
  const size_t pixel = y * HalDisplay::DISPLAY_WIDTH + x;
  return (native[pixel / 2] >> ((pixel & 1) * 4)) & 15;
}
void le16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(value);
  bytes.push_back(value >> 8);
}
void le32(std::vector<uint8_t>& bytes, uint32_t value) {
  le16(bytes, value);
  le16(bytes, value >> 16);
}
std::vector<uint8_t> ramp(bool topDown, bool reversedPalette) {
  constexpr int width = 17, height = 2, rowBytes = 12, offset = 118;
  std::vector<uint8_t> bytes;
  bytes.reserve(offset + height * rowBytes);
  le16(bytes, 0x4D42);
  le32(bytes, offset + height * rowBytes);
  le32(bytes, 0);
  le32(bytes, offset);
  le32(bytes, 40);
  le32(bytes, width);
  le32(bytes, topDown ? -height : height);
  le16(bytes, 1);
  le16(bytes, 4);
  le32(bytes, 0);
  le32(bytes, height * rowBytes);
  le32(bytes, 0);
  le32(bytes, 0);
  le32(bytes, 16);
  le32(bytes, 16);
  for (int i = 0; i < 16; ++i) {
    const uint8_t gray = (reversedPalette ? 15 - i : i) * 17;
    bytes.insert(bytes.end(), {gray, gray, gray, 0});
  }
  for (int row = 0; row < height; ++row) {
    const int y = topDown ? row : height - 1 - row;
    for (int x = 0; x < rowBytes * 2; x += 2) {
      const auto index = [=](int pixel) { return pixel < width ? (pixel + y) % 16 : 0; };
      bytes.push_back((index(x) << 4) | index(x + 1));
    }
  }
  return bytes;
}
std::pair<int, int> physical(GfxRenderer::Orientation orientation, int x, int y) {
  switch (orientation) {
    case GfxRenderer::Portrait:
      return {y, 31 - x};
    case GfxRenderer::PortraitInverted:
      return {127 - y, x};
    case GfxRenderer::LandscapeClockwise:
      return {127 - x, 31 - y};
    case GfxRenderer::LandscapeCounterClockwise:
      return {x, y};
  }
  std::abort();
}
class RecordingPrint : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t limit = SIZE_MAX;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    const size_t count = std::min(size, limit - bytes.size());
    bytes.insert(bytes.end(), data, data + count);
    return count;
  }
};
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 3);
  refreshEvents.reserve(256);
  setenv("CROSSPOINT_SIM_SD", argv[2], 1);
  GfxRenderer renderer(display);
  renderer.begin();
  levels = 4;
  assert(renderer.getGrayscaleLevels() == 4 && !renderer.beginGrayscale16());
  assert(qrParams(renderer) == "&w=32&h=128&mode=gray4");
  levels = 16;
  assert(qrParams(renderer) == "&w=32&h=128&mode=gray16");
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    // Shared logical footer geometry reaches the physical driver in every
    // orientation; no Activity-owned rotated coordinates or extra hit boxes.
    const int footerY = renderer.getScreenHeight() - 8;
    renderer.setNextGray16UiRefresh(0, footerY, renderer.getScreenWidth(), 8, 8, 32);
    const auto [ax, ay] = physical(orientation, 0, footerY);
    const auto [bx, by] = physical(orientation, renderer.getScreenWidth() - 1, footerY + 7);
    assert((uiRegion == std::array<int, 6>{std::min(ax, bx), std::min(ay, by), std::abs(bx - ax) + 1,
                                           std::abs(by - ay) + 1, 8, 32}));
    assert(renderer.beginGrayscale16());
    // Exact secondary ink, coverage and inversion, on the actual renderer.
    assert(renderer.textInkLevel() == 0);
    {
      GfxRenderer::TextInkScope ink(renderer, 4);
      assert(renderer.textInkLevel() == 4 && renderer.textInkLevel(false) == 15);
      {
        GfxRenderer::TextInkScope muted(renderer, 8);
        assert(renderer.textInkLevel() == 8);
      }
      assert(renderer.textInkLevel() == 4);
      renderer.blendGrayscale16Pixel(5, 5, renderer.textInkLevel(), 15);
      auto [px, py] = physical(orientation, 5, 5);
      assert(tone(px, py) == 4);
      renderer.blendGrayscale16Pixel(5, 5, 0, 0);
      assert(tone(px, py) == 4);  // Transparent coverage preserves the underlay.
      renderer.blendGrayscale16Pixel(5, 5, 15, 7);
      assert(tone(px, py) == 9);  // Paper glyph coverage over secondary ink.
      const GfxRenderer::ClipScope clip(renderer, 8, 8, 2, 2);
      renderer.blendGrayscale16Pixel(5, 5, 0, 15);
      assert(tone(px, py) == 9);
    }
    assert(renderer.textInkLevel() == 0);
    renderer.cancelGrayscale16();
    // Real PNGdec: RGBA, gray+alpha, indexed tRNS and truecolor/gray color keys.
    PngToFramebufferConverter png;
    for (const auto* name : {"rgba", "gray-alpha", "indexed", "rgb-key", "gray-key"}) {
      const std::string path = std::string("/") + name + ".png";
      RenderConfig cfg{};
      cfg.x = 3;
      cfg.y = 4;
      cfg.maxWidth = 16;
      cfg.maxHeight = 2;
      cfg.preserveAlpha = true;
      cfg.output = DecodeOutput::NativeGrayscale16;
      assert(!png.decodeToFramebuffer(path, renderer, cfg));
      assert(renderer.beginGrayscale16());
      native.fill(0x66);
      bw.fill(0xA5);
      assert(renderer.commitGrayscale16());
      const auto originalBw = bw;
      assert(renderer.beginRetainedGrayscale16() && !renderer.beginRetainedGrayscale16());
      assert(bw == originalBw);
      assert(png.decodeToFramebuffer(path, renderer, cfg));
      for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 16; ++x) {
          const bool keyed = std::string(name).find("key") != std::string::npos;
          const int alpha = keyed ? (x == 1 ? 0 : 15) : (x % 3 == 0 ? 0 : x % 3 == 1 ? 8 : 15);
          const int delta = (x - 6) * alpha;
          const int expected = 6 + (delta >= 0 ? (delta + 7) / 15 : -((7 - delta) / 15));
          const auto [px, py] = physical(orientation, x + 3, y + 4);
          assert(tone(px, py) == expected);
        }
      const auto [px, py] = physical(orientation, 2, 4);
      assert(tone(px, py) == 6);  // All pixels outside the sticker stay gray, not BW.
      renderer.cancelGrayscale16();
      assert(renderer.beginRetainedGrayscale16());
      for (const auto byte : native) assert(byte == 0x66);  // Partial/cancelled decode discarded.
      failDecoderAllocation = true;
      assert(!png.decodeToFramebuffer(path, renderer, cfg));
      failDecoderAllocation = false;
      failAllocation = true;
      assert(!png.decodeToFramebuffer(path, renderer, cfg));
      failAllocation = false;
      const auto heap = availableHeap;
      availableHeap = {100, 100, 100, 100};
      assert(!png.decodeToFramebuffer(path, renderer, cfg));
      availableHeap = heap;
      renderer.cancelGrayscale16();
    }
    // Row-wise fast fills match the per-pixel paths byte for byte (both buffers), with
    // odd/even nibble edges, partial B/W bytes, clipping and off-screen parts.
    {
      uint32_t seed = 12345u + static_cast<uint32_t>(orientation);
      const auto next = [&seed](int bound) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 8) % static_cast<uint32_t>(bound));
      };
      const int w = renderer.getScreenWidth();
      const int h = renderer.getScreenHeight();
      for (int round = 0; round < 40; ++round) {
        const int x = next(w + 40) - 20, y = next(h + 40) - 20;
        const int rw = next(70) + 1, rh = next(70) + 1;
        const uint8_t level = static_cast<uint8_t>(next(16));
        const bool clipped = round % 5 == 0;
        assert(renderer.beginGrayscale16());
        native.fill(0x5A);
        bw.fill(0x96);
        {
          std::optional<GfxRenderer::ClipScope> clip;
          if (clipped) clip.emplace(renderer, x + 3, y + 2, rw / 2 + 1, rh / 2 + 1);
          for (int px = x; px < x + rw; ++px)
            for (int py = y; py < y + rh; ++py)
              if (px >= 0 && py >= 0 && px < w && py < h) renderer.drawGrayscale16Pixel(px, py, level * 17);
        }
        const auto expectedNative = native;
        const auto expectedBw = bw;
        native.fill(0x5A);
        bw.fill(0x96);
        {
          std::optional<GfxRenderer::ClipScope> clip;
          if (clipped) clip.emplace(renderer, x + 3, y + 2, rw / 2 + 1, rh / 2 + 1);
          renderer.fillGrayscale16Rect(x, y, rw, rh, level);
        }
        assert(native == expectedNative && bw == expectedBw);
        // fillRect black/white matches drawPixel's mirrored 0/15.
        for (const bool black : {true, false}) {
          native.fill(0x5A);
          bw.fill(0x96);
          for (int py = y; py < y + rh; ++py)
            for (int px = x; px < x + rw; ++px)
              if (px >= 0 && py >= 0 && px < w && py < h) renderer.drawPixel(px, py, black);
          const auto pixelNative = native;
          const auto pixelBw = bw;
          native.fill(0x5A);
          bw.fill(0x96);
          renderer.fillRect(x, y, rw, rh, black);
          assert(native == pixelNative && bw == pixelBw);
        }
        renderer.cancelGrayscale16();
      }
    }
    assert(renderer.beginGrayscale16());
    renderer.fillRect(0, 0, 3, 2);
    for (int y = 0; y < renderer.getScreenHeight(); ++y) {
      for (int x = 0; x < renderer.getScreenWidth(); ++x) {
        const auto [px, py] = physical(orientation, x, y);
        assert(tone(px, py) == (x < 3 && y < 2 ? 0 : 15));
      }
    }
    renderer.cancelGrayscale16();
    for (const bool topDown : {false, true})
      for (const bool reversed : {false, true}) {
        auto bytes = ramp(topDown, reversed);
        Bitmap bitmap(bytes.data(), bytes.size());
        assert(bitmap.parseHeaders() == BmpReaderError::Ok);
        assert(renderer.beginGrayscale16() && !renderer.beginGrayscale16());
        assert(renderer.drawBitmapGrayscale16(bitmap, 2, 3, 17, 2));
        for (int y = 0; y < 2; ++y)
          for (int x = 0; x < 17; ++x) {
            const auto [px, py] = physical(orientation, x + 2, y + 3);
            const int expected = reversed ? 15 - (x + y) % 16 : (x + y) % 16;
            assert(tone(px, py) == expected);
          }
        assert(renderer.commitGrayscale16() && !renderer.commitGrayscale16());
        bitmap.rewindToData();
        assert(renderer.beginGrayscale16());
        assert(renderer.drawBitmapGrayscale16(bitmap, 0, 0, 8, 1));
        renderer.cancelGrayscale16();
        assert(!loan);
        bytes.pop_back();
        Bitmap truncated(bytes.data(), bytes.size());
        assert(truncated.parseHeaders() == BmpReaderError::Ok);
        assert(renderer.beginGrayscale16());
        assert(!renderer.drawBitmapGrayscale16(truncated, 0, 0, 17, 2));
        renderer.cancelGrayscale16();
        Bitmap oom(bytes.data(), bytes.size());
        assert(oom.parseHeaders() == BmpReaderError::Ok && renderer.beginGrayscale16());
        failAllocation = true;
        assert(!renderer.drawBitmapGrayscale16(oom, 0, 0, 17, 2));
        failAllocation = false;
        renderer.cancelGrayscale16();
      }
  }
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  std::ifstream source(argv[1], std::ios::binary);
  std::ofstream copied(std::string(argv[2]) + "/ramp.jpg", std::ios::binary);
  copied << source.rdbuf();
  copied.close();
  JpegToFramebufferConverter jpeg;
  RenderConfig config{};
  config.x = 0;
  config.y = 0;
  config.maxWidth = 128;
  config.maxHeight = 16;
  config.output = DecodeOutput::NativeGrayscale16;
  config.useDithering = true;  // Native branch must bypass four-level dithering.
  assert(!jpeg.decodeToFramebuffer("/ramp.jpg", renderer, config));
  assert(renderer.beginGrayscale16());
  assert(jpeg.decodeToFramebuffer("/ramp.jpg", renderer, config));
  for (int i = 0; i < 16; ++i) assert(tone(i * 8 + 4, 8) == i);
  failRefresh = true;
  const int cancelsBeforeFailedCommit = cancels;
  assert(!renderer.commitGrayscale16() && !renderer.isGrayscale16Active());
  assert(cancels == cancelsBeforeFailedCommit + 1);
  failRefresh = false;
  HalFile file;
  assert(Storage.openFileForRead("TEST", "/ramp.jpg", file));
  RecordingPrint out;
  assert(JpegToBmpConverter::jpegFileToBmpStream(file, out, false, JpegToBmpConverter::Output::Gray8));
  Bitmap saved(out.bytes.data(), out.bytes.size());
  assert(saved.parseHeaders() == BmpReaderError::Ok && saved.getBpp() == 8);
  assert(renderer.beginGrayscale16());
  assert(renderer.drawBitmapGrayscale16(saved, 0, 0, 128, 32));
  for (int i = 0; i < 16; ++i) assert(tone(i * 2 + 1, 2) == i);
  assert(renderer.commitGrayscale16());
  for (const size_t limit : {size_t(3), size_t(1079)}) {
    assert(file.seek(0));
    RecordingPrint shortOut;
    shortOut.limit = limit;
    assert(!JpegToBmpConverter::jpegFileToBmpStream(file, shortOut, false, JpegToBmpConverter::Output::Gray8));
  }
  assert(file.seek(0));
  RecordingPrint legacy;
  assert(JpegToBmpConverter::jpegFileToBmpStream(file, legacy, false));
  Bitmap old(legacy.bytes.data(), legacy.bytes.size());
  assert(old.parseHeaders() == BmpReaderError::Ok && old.getBpp() == 2);
  // Image previews fit the oriented viewport, while thumbnail callers retain crop by default.
  for (const auto& target : {std::pair{32, 128}, std::pair{128, 32}}) {
    assert(file.seek(0));
    RecordingPrint preview;
    assert(JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, preview, target.first, target.second, false));
    Bitmap fitted(preview.bytes.data(), preview.bytes.size());
    assert(fitted.parseHeaders() == BmpReaderError::Ok && fitted.getBpp() == 2);
    assert(fitted.getWidth() == target.first && fitted.getHeight() == target.first / 8);
    assert(fitted.getWidth() <= target.first && fitted.getHeight() <= target.second);
  }
  assert(file.seek(0));
  RecordingPrint thumbnail;
  assert(JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, thumbnail, 32, 32));
  Bitmap cropped(thumbnail.bytes.data(), thumbnail.bytes.size());
  assert(cropped.parseHeaders() == BmpReaderError::Ok && cropped.getWidth() == 256 && cropped.getHeight() == 32);
  airpage::SelectedImage selected;
  std::strcpy(selected.path, "/ramp.jpg");
  selected.image = {airpage::ImageFormat::Jpeg, 128, 16, true};
  const Rect viewport(0, 0, 128, 16);
  const int before = commits;
  refreshEvents.clear();
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected, true) ==
         airpage::AirPageImageRenderer::Result::Success);
  assert(commits == before + 1 && !loan);
  assert((refreshEvents == std::vector<char>{'w', 'w', 'B', 'C'}));
  // A popup paints the B/W proxy; closing it re-renders the unchanged original.
  renderer.clearScreen();
  refreshEvents.clear();
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected) ==
         airpage::AirPageImageRenderer::Result::Success);
  assert((refreshEvents == std::vector<char>{'B', 'C'}));
  for (int i = 0; i < 16; ++i) assert(tone(i * 8 + 4, 8) == i);
  failRefresh = true;
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected) ==
         airpage::AirPageImageRenderer::Result::Failed);
  failRefresh = false;
  assert(!loan);
  // All heap-backed JPEG entry points use allocator capacity, not internal-only RAM.
  const auto healthyHeap = availableHeap;
  const size_t decoderBytes = sizeof(JPEGDEC);
  ImageDimensions dimensions{};
  ImageRenderError error = ImageRenderError::None;
  config.error = &error;
  for (const auto heap :
       {HalMemory::HeapStats{decoderBytes + 16383, 100000, 0, decoderBytes},
        HalMemory::HeapStats{decoderBytes + 16384, 100000, 0, decoderBytes - 1}, HalMemory::getInternalHeap()}) {
    availableHeap = heap;
    assert(!JpegToFramebufferConverter::getDimensionsStatic("/ramp.jpg", dimensions));
    assert(renderer.beginGrayscale16());
    assert(!jpeg.decodeToFramebuffer("/ramp.jpg", renderer, config));
    assert(error == ImageRenderError::OutOfMemory);
    renderer.cancelGrayscale16();
    assert(file.seek(0));
    RecordingPrint rejected;
    assert(!JpegToBmpConverter::jpegFileToBmpStream(file, rejected));
    assert(rejected.bytes.empty());
    assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected) ==
           airpage::AirPageImageRenderer::Result::OutOfMemory);
    assert(!loan && Storage.exists("/ramp.jpg"));
  }
  availableHeap = {decoderBytes + 16384, 100000, 0, decoderBytes};
  assert(JpegToFramebufferConverter::getDimensionsStatic("/ramp.jpg", dimensions));
  // A borrowed framebuffer still permits conversion with an otherwise empty heap.
  std::vector<uint8_t> scratch(decoderBytes);
  buildscratch::lend(scratch.data(), scratch.size());
  availableHeap = {};
  assert(file.seek(0));
  RecordingPrint borrowed;
  assert(JpegToBmpConverter::jpegFileToBmpStream(file, borrowed));
  RenderConfig cacheConfig = config;
  cacheConfig.output = DecodeOutput::CacheOnly;
  cacheConfig.cachePath = "/scratch.pxc";
  assert(jpeg.decodeToFramebuffer("/ramp.jpg", renderer, cacheConfig));
  auto* released = buildscratch::claim(decoderBytes);
  assert(released == scratch.data());
  buildscratch::release(released);
  buildscratch::reclaim();
  availableHeap = healthyHeap;
  failDecoderAllocation = true;  // Preflight passes but the actual allocation fails.
  assert(!JpegToFramebufferConverter::getDimensionsStatic("/ramp.jpg", dimensions));
  assert(file.seek(0));
  RecordingPrint allocationRejected;
  assert(!JpegToBmpConverter::jpegFileToBmpStream(file, allocationRejected));
  assert(allocationRejected.bytes.empty());
  assert(renderer.beginGrayscale16());
  assert(!jpeg.decodeToFramebuffer("/ramp.jpg", renderer, config));
  assert(error == ImageRenderError::OutOfMemory);
  renderer.cancelGrayscale16();
  refreshEvents.clear();
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected, true) ==
         airpage::AirPageImageRenderer::Result::OutOfMemory);
  assert(!loan);
  assert((refreshEvents == std::vector<char>{'w', 'w', 'B', 'X'}));
  failDecoderAllocation = false;
  assert(renderer.beginGrayscale16());
  assert(jpeg.decodeToFramebuffer("/ramp.jpg", renderer, config));
  assert(error == ImageRenderError::None);
  renderer.cancelGrayscale16();
  // Exit cleanup cancels an active transaction and consumes either override.
  for (const auto overrideMode : {HalDisplay::FULL_REFRESH, HalDisplay::HALF_REFRESH}) {
    refreshEvents.clear();
    assert(renderer.beginGrayscale16());
    renderer.drawPixel(1, 1, 0);
    renderer.requestNextRefresh(overrideMode);
    airpage::AirPageImageRenderer::cleanScreen(renderer);
    assert(!loan && !renderer.isGrayscale16Active());
    assert((refreshEvents == std::vector<char>{'B', 'X', 'w', 'w'}));
    // The next activity's frame must not inherit the old override.
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    assert(refreshEvents.back() == 'w');
  }
  // Legacy output replaces its existing preclear with two white fast clears.
  auto bmpBytes = ramp(false, false);
  std::ofstream legacyBmpOut(std::string(argv[2]) + "/legacy-ramp.bmp", std::ios::binary);
  legacyBmpOut.write(reinterpret_cast<const char*>(bmpBytes.data()), bmpBytes.size());
  legacyBmpOut.close();
  airpage::SelectedImage bmpSelected;
  std::strcpy(bmpSelected.path, "/legacy-ramp.bmp");
  bmpSelected.image = {airpage::ImageFormat::Bmp, 17, 2, true};
  levels = 4;
  refreshEvents.clear();
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, bmpSelected, true) ==
         airpage::AirPageImageRenderer::Result::Success);
  assert((refreshEvents == std::vector<char>{'w', 'w', 'f', 'G'}));
  refreshEvents.clear();
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, bmpSelected) ==
         airpage::AirPageImageRenderer::Result::Success);
  assert((refreshEvents == std::vector<char>{'w', 'f', 'G'}));
  levels = 4;
  legacyError = ImageRenderError::OutOfMemory;
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected) ==
         airpage::AirPageImageRenderer::Result::OutOfMemory);
  legacyError = ImageRenderError::Failed;
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected) ==
         airpage::AirPageImageRenderer::Result::Failed);
  levels = 16;
  assert(airpage::AirPageWallpaper::install(selected));
  assert(SETTINGS.sleepScreen == CrossPointSettings::CUSTOM);
  HalFile sleep;
  assert(Storage.openFileForRead("TEST", "/sleep.bmp", sleep));
  Bitmap sleepBitmap(sleep);
  assert(sleepBitmap.parseHeaders() == BmpReaderError::Ok && sleepBitmap.getBpp() == 8);
  // A settings failure must restore the previous original BMP and delete .part/.bak.
  const auto bmp = ramp(false, true);
  std::ofstream bmpFile(std::string(argv[2]) + "/ramp.bmp", std::ios::binary);
  bmpFile.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
  bmpFile.close();
  std::strcpy(selected.path, "/ramp.bmp");
  selected.image = {airpage::ImageFormat::Bmp, 17, 2, true};
  SETTINGS.failedSaves = 1;
  assert(!airpage::AirPageWallpaper::install(selected));
  airpage::ImageInfo retained;
  assert(airpage::AirPageImageStore::inspectImage("/sleep.bmp", retained) && retained.width == 32);
  assert(!Storage.exists("/sleep.bmp.part") && !Storage.exists("/sleep.bmp.bak"));
  assert(airpage::AirPageWallpaper::install(selected));
  assert(airpage::AirPageImageStore::inspectImage("/sleep.bmp", retained) && retained.width == 17);
  SleepProbe sleepProbe(renderer);
  const int beforeSleep = commits;
  sleepProbe.renderCustomSleepScreen();
  assert(commits == beforeSleep + 1 && sleepProbe.defaults == 0 && !loan);
  failRefresh = true;
  sleepProbe.renderCustomSleepScreen();
  assert(sleepProbe.defaults == 1 && !loan);
  failRefresh = false;
  SETTINGS.sleepScreenCoverFilter = CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::BLACK_AND_WHITE;
  const int beforeFiltered = commits;
  sleepProbe.renderCustomSleepScreen();
  assert(commits == beforeFiltered);  // Existing filters must not enter native output.
  SETTINGS.sleepScreenCoverFilter = CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  assert(commits > 0 && cancels > 0);
}
