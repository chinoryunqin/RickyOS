#pragma once

#include "ReadingMode.h"

class GfxRenderer;

// Draws what a reading mode puts around the text: the puppy frame and dog, or the
// ancient-book rules and 版心. Called with the status bar in every render pass
// (B/W base, 16-level page, gray planes), so it draws with the plain B/W API and
// only adds gray where the 16-level frame is open. No heap; Flash art only.
namespace readerChrome {

struct PageInfo {
  const char* bookTitle = nullptr;
  int pageInChapter = 1;  // 1-based
};

void draw(const GfxRenderer& renderer, readingMode::Mode mode, const readingMode::Box& text, const PageInfo& page);

}  // namespace readerChrome
