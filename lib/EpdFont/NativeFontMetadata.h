#pragma once

struct EpdFontData;

namespace NativeFontMetadata {
// Boot task only, before any font use. Both descriptors remain empty on failure.
bool initialize(EpdFontData& regular, EpdFontData& medium);
}
