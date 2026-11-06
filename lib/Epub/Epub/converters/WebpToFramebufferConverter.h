#pragma once
#include "ImageToFramebufferDecoder.h"
#if RICKYOS_WEBP
class WebpToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  bool decodeToFramebuffer(const std::string&, GfxRenderer&, const RenderConfig&) override;
  bool getDimensions(const std::string&, ImageDimensions&) const override;
  const char* getFormatName() const override { return "WebP"; }
  static bool supportsFormat(const std::string& extension) { return extension == ".webp"; }
  static bool toBmp(HalFile& input, Print& output, int maxWidth, int maxHeight, bool crop);
};
#endif
