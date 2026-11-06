#pragma once

#include <algorithm>
#include <cstdint>

// One reader-owned table, rebuilt only when settings change. No per-image heap
// or second framebuffer. Apply after interpolation and before quantization.
class ImageToneCurve {
 public:
  static constexpr int kContrastLimit = 50;
  static constexpr int kBrightnessLimit = 40;
  static constexpr int kMinimumBrightness = -55;  // Reader UI -40 relative to its -15 baseline.
  // Gain (60+c)/(60-c): 0 is neutral, +20 is 2x, +30 is 3x,
  // +50 is 11x. Negative values use the reciprocal gain. The finite
  // denominator keeps the entire signed control range safe with integer math.
  static constexpr int kContrastPivot = 60;
  static constexpr uint32_t kContrastAlgorithmIdentity = 1u << 17;
  static constexpr uint32_t profileIdentity(int contrast, int brightness, bool smooth) {
    return uint32_t{static_cast<uint8_t>(contrast)} | (uint32_t{static_cast<uint8_t>(brightness)} << 8) |
           (uint32_t{smooth} << 16) | (contrast != 0 ? kContrastAlgorithmIdentity : 0);
  }

  bool set(int contrast, int brightness) {
    contrast = std::clamp(contrast, -kContrastLimit, kContrastLimit);
    brightness = std::clamp(brightness, kMinimumBrightness, kBrightnessLimit);
    if (contrast == contrast_ && brightness == brightness_) return false;
    contrast_ = contrast;
    brightness_ = brightness;
    const int numerator = kContrastPivot + contrast;
    const int denominator = kContrastPivot - contrast;
    for (int sample = 0; sample < 256; ++sample) {
      const int scaled = (sample - 128) * numerator;
      const int delta =
          scaled >= 0 ? (scaled + denominator / 2) / denominator : -((-scaled + denominator / 2) / denominator);
      const int adjusted = 128 + delta + brightness * 255 / 100;
      values_[sample] = static_cast<uint8_t>(std::clamp(adjusted, 0, 255));
    }
    return true;
  }

  const uint8_t* data() const { return contrast_ == 0 && brightness_ == 0 ? nullptr : values_; }
  uint32_t identity(bool smooth) const { return profileIdentity(contrast_, brightness_, smooth); }

 private:
  int contrast_ = 0;
  int brightness_ = 0;
  uint8_t values_[256]{};
};
