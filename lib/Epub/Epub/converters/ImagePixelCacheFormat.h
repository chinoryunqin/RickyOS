#pragma once
#include <cstddef>
#include <cstdint>
namespace imagePixelCache {
inline constexpr uint32_t kMagicV2 = 0x32435850;  // PXC2, little endian
inline constexpr size_t kHeaderBytes = 12;
}  // namespace imagePixelCache
