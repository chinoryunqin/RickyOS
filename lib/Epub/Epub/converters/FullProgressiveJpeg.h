#pragma once

#include <string>

class GfxRenderer;
struct RenderConfig;

// Full coefficients, native gray target only. Never falls back to DC-only previews.
bool decodeFullProgressiveJpeg(const std::string& path, GfxRenderer& renderer, const RenderConfig& config);
