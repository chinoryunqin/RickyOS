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
// The default avatar: the brand badge, framed by its own ring.
void drawBrandAvatar(const GfxRenderer& renderer, const Rect& rect);
// Built-in line-art portraits live in the avatar setting as "@avatar:<index>".
// Returns the selected portrait, or -1 for the brand badge or an imported image.
int presetAvatarIndex();
void drawPresetAvatar(const GfxRenderer& renderer, const Rect& rect, int index);
// "" restores the brand badge. Saves settings; the previous value returns on failure.
bool setAvatar(const char* value);
// The card's visible rectangle is also its only touch target.
void drawCard(UiScreen& screen, const GfxRenderer& renderer, freeink::ui::Rect rect, freeink::ui::ActionId action,
              int value = 0);
bool importAvatar(const std::string& path);
}  // namespace RickyProfile
#endif
