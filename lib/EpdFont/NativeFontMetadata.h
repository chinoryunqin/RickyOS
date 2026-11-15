#pragma once

struct EpdFontData;

namespace NativeFontMetadata {
// Boot task only, before any font use. The descriptor remains empty on failure.
bool initialize(EpdFontData& regular);
}
