#pragma once

// A physical pixel budget for the ordinary reader. Keep the same product
// hierarchy and widgets; never shrink an already-rasterized page to fit it.
#if FREEINK_DEVICE_METALIO_EINK4 || defined(SIMULATOR_DEVICE_METALIO_EINK4)
#define RICKYOS_NATIVE_SMALL_PANEL 1
#else
#define RICKYOS_NATIVE_SMALL_PANEL 0
#endif

namespace ricky::panel {
inline constexpr bool nativeSmall = RICKYOS_NATIVE_SMALL_PANEL;
constexpr int pixels(int standard, int small) { return nativeSmall ? small : standard; }
}  // namespace ricky::panel
