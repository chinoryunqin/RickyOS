#include "WebpToFramebufferConverter.h"
#if RICKYOS_WEBP
#include <GfxRenderer.h>
#include <Memory.h>
#include <RickyWebpMemory.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"
#include "WebpRaster.h"
namespace {
uint8_t alphaThreshold4x4(int x,int y) {
  static constexpr uint8_t values[16]={0,128,32,160,192,64,224,96,48,176,16,144,240,112,208,80};
  return values[((y&3)<<2)|(x&3)];
}
uint8_t grayOf(const uint8_t* p, bool flatten) {
  const int gray = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
  return flatten ? uint8_t((gray * p[3] + 255 * (255 - p[3]) + 127) / 255) : uint8_t(gray);
}
struct DrawContext {
  GfxRenderer& renderer;
  const RenderConfig& config;
};
bool draw(const uint8_t* rgba, int w, int h, void* opaque) {
  auto& ctx = *static_cast<DrawContext*>(opaque);
  auto& r = ctx.renderer;
  const auto& c = ctx.config;
  const bool native = c.output == DecodeOutput::NativeGrayscale16;
  const bool cacheOnly = c.output == DecodeOutput::CacheOnly;
  DirectPixelWriter pw;
  if (!cacheOnly && !native) pw.init(r);
  PixelCache cache;
  bool caching = !native && !c.preserveAlpha && !c.cachePath.empty();
  if (caching && !cache.begin(c.cachePath, w, h, c.x, c.y, 1, c.cacheIdentity)) {
    if (cacheOnly) return false;
    caching = false;
  }
  const ScopedCleanup abortCache{[&] {
    if (cache.ok) cache.abort();
  }};
  uint32_t yield = 0;
  for (int y = 0; y < h; ++y) {
    if (c.cancellation.isCancelled()) return false;
    ImageToFramebufferDecoder::yieldDuringDecode(yield);
    const int oy = c.y + y;
    if (caching && !cache.advanceTo(y)) return false;
    DirectCacheWriter cw;
    if (caching) {
      cw.init(cache.buffer, cache.bytesPerRow, cache.bandRows, cache.originX);
      cw.beginRow(oy, c.y + cache.bandStart);
    }
    if (!cacheOnly && !native) pw.beginRow(oy);
    for (int x = 0; x < w; ++x) {
      const int ox = c.x + x;
      if (ox < 0 || oy < 0 || ox >= r.getScreenWidth() || oy >= r.getScreenHeight()) continue;
      const uint8_t* p = rgba + (size_t(y) * w + x) * 4;
      const uint8_t g = c.adjustTone(grayOf(p, !c.preserveAlpha));
      const uint8_t a = c.preserveAlpha ? p[3] : 255;
      if (native) {
        r.blendGrayscale16Pixel(ox, oy, (g + 8) / 17, (a + 8) / 17);
        continue;
      }
      if (a < 8 || a <= alphaThreshold4x4(ox, oy)) continue;
      const uint8_t level = c.useDithering ? applyBayerDither4Level(g, ox, oy) : (g >> 6);
      if (!cacheOnly) pw.writePixel(ox, level, c.preserveAlpha);
      if (caching) cw.writePixel(ox, level);
    }
  }
  if (cacheOnly && !caching) return false;
  return !caching || cache.finalize(c.cancellation);
}
void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
struct BmpContext {
  Print& output;
};
bool bmp(const uint8_t* rgba, int w, int h, void* opaque) {
  auto& out = static_cast<BmpContext*>(opaque)->output;
  const size_t stride = (size_t(w) + 3) & ~size_t(3);
  const size_t imageSize = stride * h;
  uint8_t header[54]{};
  header[0] = 'B';
  header[1] = 'M';
  put32(header + 2, 1078 + imageSize);
  put32(header + 10, 1078);
  put32(header + 14, 40);
  put32(header + 18, w);
  put32(header + 22, uint32_t(-h));
  header[26] = 1;
  header[28] = 8;
  put32(header + 34, imageSize);
  put32(header + 38, 2835);
  put32(header + 42, 2835);
  put32(header + 46, 256);
  put32(header + 50, 256);
  if (out.write(header, sizeof(header)) != sizeof(header)) return false;
  for (int i = 0; i < 256; ++i) {
    uint8_t p[4]{uint8_t(i), uint8_t(i), uint8_t(i), 0};
    if (out.write(p, 4) != 4) return false;
  }
  auto* row = static_cast<uint8_t*>(RickyWebpMalloc(stride));
  if (!row) return false;
  const ScopedCleanup release{[&] { RickyWebpFree(row); }};
  memset(row, 0, stride);
  uint32_t yield = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) row[x] = grayOf(rgba + (size_t(y) * w + x) * 4, true);
    if (out.write(row, stride) != stride) return false;
    ImageToFramebufferDecoder::yieldDuringDecode(yield);
  }
  return true;
}
}  // namespace
bool WebpToFramebufferConverter::getDimensions(const std::string& path, ImageDimensions& dims) const {
  HalFile f;
  return Storage.openFileForRead("WEBP", path, f) && webpFileDimensions(f, dims);
}
bool WebpToFramebufferConverter::decodeToFramebuffer(const std::string& path, GfxRenderer& r, const RenderConfig& c) {
  if (c.error) *c.error = ImageRenderError::Failed;
  if (c.output == DecodeOutput::NativeGrayscale16 && !r.isGrayscale16Active()) return false;
  if (c.maxWidth <= 0 || c.maxHeight <= 0 || !std::isfinite(c.sourceCropX) || !std::isfinite(c.sourceCropY))
    return false;
  HalFile f;
  ImageDimensions dims{};
  if (!Storage.openFileForRead("WEBP", path, f) || !webpFileDimensions(f, dims)) return false;
  WebpDecodeSpec spec;
  spec.left = int(dims.width * std::clamp(c.sourceCropX, 0.f, .99f) / 2) & ~1;
  spec.top = int(dims.height * std::clamp(c.sourceCropY, 0.f, .99f) / 2) & ~1;
  spec.cropWidth = dims.width - 2 * spec.left;
  spec.cropHeight = dims.height - 2 * spec.top;
  if (c.useExactDimensions) {
    spec.width = c.maxWidth;
    spec.height = c.maxHeight;
  } else {
    float scale = std::min({float(c.maxWidth) / spec.cropWidth, float(c.maxHeight) / spec.cropHeight, 1.f});
    spec.width = std::max(1, int(spec.cropWidth * scale));
    spec.height = std::max(1, int(spec.cropHeight * scale));
  }
  DrawContext ctx{r, c};
  return decodeWebpRaster(f, spec, draw, &ctx, c.cancellation, c.error);
}
bool WebpToFramebufferConverter::toBmp(HalFile& f, Print& out, int maxWidth, int maxHeight, bool crop) {
  ImageDimensions dims{};
  if (maxWidth <= 0 || maxHeight <= 0 || !webpFileDimensions(f, dims)) return false;
  WebpDecodeSpec spec;
  spec.cropWidth = dims.width;
  spec.cropHeight = dims.height;
  if (crop) {
    const double ratio = double(maxWidth) / maxHeight;
    if (double(dims.width) / dims.height > ratio) {
      spec.cropWidth = std::max(1, int(dims.height * ratio));
      spec.left = ((dims.width - spec.cropWidth) / 2) & ~1;
    } else {
      spec.cropHeight = std::max(1, int(dims.width / ratio));
      spec.top = ((dims.height - spec.cropHeight) / 2) & ~1;
    }
    spec.width = maxWidth;
    spec.height = maxHeight;
  } else {
    const double scale = std::min(double(maxWidth) / dims.width, double(maxHeight) / dims.height);
    spec.width = std::max(1, int(dims.width * scale));
    spec.height = std::max(1, int(dims.height * scale));
  }
  BmpContext ctx{out};
  return decodeWebpRaster(f, spec, bmp, &ctx);
}
#endif
