#ifdef RICKYOS_PRODUCT
#include "KeepPageFace.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "components/UITheme.h"

// The power-off "keep the page" screen does the same (SleepActivity::renderLastScreenSleepScreen):
// a FAST difference adds the label without the clean pass that would flash the page.
bool KeepPageFace::renderNative(GfxRenderer& renderer, const Rect&) {
  GUI.drawRickyStandbyIndicator(renderer);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}
#endif
