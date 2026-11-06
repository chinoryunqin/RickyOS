#pragma once

#include "components/RickyPanelProfile.h"
#include "components/UiHighDpiProfile.h"

// Product reader navigation is independent of the physical display density.
// The ordinary panel uses its native design tokens and fonts, never a scaled raster.
namespace readerUi {
inline constexpr bool enabled = UiHighDpiProfile::enabled || ricky::panel::nativeSmall;
}  // namespace readerUi
