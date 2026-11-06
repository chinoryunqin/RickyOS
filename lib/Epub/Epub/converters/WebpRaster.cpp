#include "WebpRaster.h"
#if RICKYOS_WEBP
#include <Logging.h>
#include <Memory.h>
#include <RickyWebpMemory.h>
#include <webp/decode.h>

#include <algorithm>
#include <cstring>
namespace {
bool features(HalFile& file, WebPBitstreamFeatures& f) {
  if (!file.seek(0)) return false;
  uint8_t header[64]{};
  const int n = file.read(header, sizeof(header));
  if (n < 20 || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WEBP", 4)) return false;
  const uint32_t size =
      uint32_t(header[4]) | (uint32_t(header[5]) << 8) | (uint32_t(header[6]) << 16) | (uint32_t(header[7]) << 24);
  if (size < 12 || uint64_t(size) + 8 > file.size()) return false;
  return WebPGetFeatures(header, n, &f) == VP8_STATUS_OK && !f.has_animation;
}
}  // namespace
bool webpFileDimensions(HalFile& file, ImageDimensions& dims) {
  WebPBitstreamFeatures f{};
  return features(file, f) && ImageToFramebufferDecoder::validateAndStoreDimensions(f.width, f.height, dims, "WebP");
}
bool decodeWebpRaster(HalFile& file, const WebpDecodeSpec& spec, WebpRasterConsumer consume, void* context,
                      CancelCheck cancellation, ImageRenderError* error) {
  if (error) *error = ImageRenderError::Failed;
  if (cancellation.isCancelled()) {
    if (error) *error = ImageRenderError::Cancelled;
    return false;
  }
  WebPDecoderConfig config{};
  ImageDimensions dims{};
  if (!WebPInitDecoderConfig(&config) || !features(file, config.input) ||
      !ImageToFramebufferDecoder::validateAndStoreDimensions(config.input.width, config.input.height, dims, "WebP") ||
      file.size() > 16u * 1024u * 1024u || spec.width <= 0 || spec.height <= 0 || spec.width > 32767 ||
      spec.height > 32767 || uint64_t(spec.width) * spec.height > 2097152 || !consume)
    return false;
  if (!RickyWebpBegin()) {
    LOG_INF("WEBP", "Decoder busy");
    return false;
  }
  const ScopedCleanup end{[] { RickyWebpEnd(); }};
  const size_t outputSize = size_t(spec.width) * size_t(spec.height) * 4;
  auto* rgba = static_cast<uint8_t*>(RickyWebpMalloc(outputSize));
  const ScopedCleanup releaseOutput{[&] { RickyWebpFree(rgba); }};
  auto* chunk = static_cast<uint8_t*>(RickyWebpMalloc(4096));
  const ScopedCleanup releaseChunk{[&] { RickyWebpFree(chunk); }};
  if (!rgba || !chunk) {
    if (error) *error = ImageRenderError::OutOfMemory;
    return false;
  }
  config.output.colorspace = MODE_RGBA;
  config.output.is_external_memory = 1;
  config.output.u.RGBA.rgba = rgba;
  config.output.u.RGBA.stride = spec.width * 4;
  config.output.u.RGBA.size = outputSize;
  config.options.use_threads = 0;
  const int visibleWidth = spec.cropWidth > 0 ? spec.cropWidth : dims.width;
  const int visibleHeight = spec.cropHeight > 0 ? spec.cropHeight : dims.height;
  config.options.use_scaling = spec.width != visibleWidth || spec.height != visibleHeight;
  config.options.scaled_width = spec.width;
  config.options.scaled_height = spec.height;
  if (spec.cropWidth > 0 && spec.cropHeight > 0) {
    if (spec.left < 0 || spec.top < 0 || spec.left + spec.cropWidth > dims.width ||
        spec.top + spec.cropHeight > dims.height)
      return false;
    config.options.use_cropping = 1;
    config.options.crop_left = spec.left;
    config.options.crop_top = spec.top;
    config.options.crop_width = spec.cropWidth;
    config.options.crop_height = spec.cropHeight;
  }
  WebPIDecoder* decoder = WebPIDecode(nullptr, 0, &config);
  const ScopedCleanup releaseDecoder{[&] {
    if (decoder) WebPIDelete(decoder);
    WebPFreeDecBuffer(&config.output);
  }};
  if (!decoder) {
    if (error && RickyWebpAllocationFailed()) *error = ImageRenderError::OutOfMemory;
    return false;
  }
  if (!file.seek(0)) return false;
  VP8StatusCode status = VP8_STATUS_SUSPENDED;
  uint32_t lastYield = 0;
  while (status == VP8_STATUS_SUSPENDED) {
    if (cancellation.isCancelled()) {
      if (error) *error = ImageRenderError::Cancelled;
      return false;
    }
    const int n = file.read(chunk, 4096);
    if (n <= 0) break;
    status = WebPIAppend(decoder, chunk, n);
    ImageToFramebufferDecoder::yieldDuringDecode(lastYield);
  }
  if (cancellation.isCancelled()) {
    if (error) *error = ImageRenderError::Cancelled;
    return false;
  }
  if (status != VP8_STATUS_OK) {
    LOG_ERR("WEBP", "Decode failed status=%d peak=%u", int(status), unsigned(RickyWebpPeak()));
    if (error && RickyWebpAllocationFailed()) *error = ImageRenderError::OutOfMemory;
    return false;
  }
  int rows = 0, w = 0, h = 0, stride = 0;
  if (!WebPIDecGetRGB(decoder, &rows, &w, &h, &stride) || rows != spec.height || w != spec.width || h != spec.height ||
      stride != spec.width * 4)
    return false;
  if (!consume(rgba, w, h, context)) {
    if (error && cancellation.isCancelled()) *error = ImageRenderError::Cancelled;
    return false;
  }
  LOG_INF("WEBP", "Decoded %dx%d -> %dx%d, peak=%u", dims.width, dims.height, w, h, unsigned(RickyWebpPeak()));
  if (error) *error = ImageRenderError::None;
  return true;
}
#endif
