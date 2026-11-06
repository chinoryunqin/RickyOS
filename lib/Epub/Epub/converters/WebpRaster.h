#pragma once
#include "ImageToFramebufferDecoder.h"
#if RICKYOS_WEBP
struct WebpDecodeSpec {
  int width = 0, height = 0, left = 0, top = 0, cropWidth = 0, cropHeight = 0;
};
using WebpRasterConsumer = bool (*)(const uint8_t*, int, int, void*);
bool webpFileDimensions(HalFile& file, ImageDimensions& dims);
bool decodeWebpRaster(HalFile& file, const WebpDecodeSpec& spec, WebpRasterConsumer consume, void* context,
                      CancelCheck cancellation = {}, ImageRenderError* error = nullptr);
#endif
