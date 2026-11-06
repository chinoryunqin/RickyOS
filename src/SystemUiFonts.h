#pragma once
#include <VectorFontSupport.h>

#include <cstdint>
#if defined(RICKYOS_PRODUCT) && CROSSPOINT_VECTOR_FONTS && \
    (FREEINK_DEVICE_READPICO || FREEINK_DEVICE_METALIO_PLUS || FREEINK_DEVICE_METALIO_EINK4 || defined(SIMULATOR_DEVICE_READPICO) || \
     defined(SIMULATOR_DEVICE_METALIO_EINK4))
#define RICKYOS_SYSTEM_UI_FONTS 1
#else
#define RICKYOS_SYSTEM_UI_FONTS 0
#endif
class GfxRenderer;
// Call with RenderLock held. Sources and faces are owned independently of the reader.
namespace systemUiFonts {
inline constexpr uint8_t minScale = 80, maxScale = 140, defaultScale = 100;
void ensureLoaded(GfxRenderer& renderer);
void preview(const char* family, uint8_t scale);
void clearPreview();
uint8_t scale();
void suspend(GfxRenderer& renderer);
void resume();
bool customAvailable();
bool ownsFont(int id);
}  // namespace systemUiFonts
