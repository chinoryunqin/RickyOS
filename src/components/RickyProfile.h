#pragma once
#ifdef RICKYOS_PRODUCT
#include "Rect.h"
#include "UiAppHost.h"

namespace RickyProfile {
using UiScreen = UiAppHost::UiScreen;
const char* nickname();
const char* homePhrase();
bool setHomePhrase(const std::string& text);
void drawAvatar(const GfxRenderer& renderer, const Rect& rect);
// The card's visible rectangle is also its only touch target.
void drawCard(UiScreen& screen, const GfxRenderer& renderer, freeink::ui::Rect rect, freeink::ui::ActionId action,
              int value = 0);
bool importAvatar(const std::string& path);
}  // namespace RickyProfile
#endif
