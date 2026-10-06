#pragma once
#ifdef RICKYOS_PRODUCT

class GfxRenderer;

// The Standby clock: large anti-aliased digits blended into the active 16-gray frame
// (src/components/fonts/rickyClockDigits.h). Callers check isGrayscale16Active().
namespace RickyClockDigits {

int capHeight();
// Width of "HH:MM"-style text drawn by draw(); digits share one cell width.
int width(const char* text);
// Blend text into the 16-gray frame with its digits' top at yTop.
void draw(GfxRenderer& renderer, int x, int yTop, const char* text);
// Fade the picture towards paper from transparent at `top` to `strength` (0..15 of 15)
// at `fullAt`, keeping that strength down to `bottom`: the clock stays legible over
// any picture without a box around it.
void fadeToPaper(GfxRenderer& renderer, int left, int top, int width, int fullAt, int bottom, int strength);

}  // namespace RickyClockDigits
#endif
