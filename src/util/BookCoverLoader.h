#pragma once

#include <string>

class Bitmap;

namespace BookCoverLoader {

std::string ensureThumbnail(const std::string& bookPath, int height, bool* generated = nullptr);
// The file ensureThumbnail() would return for an EPUB, without touching the SD card;
// empty for other formats.
std::string thumbnailPath(const std::string& bookPath, int height);
// Drop what this process remembers about covers (books without one, drawn cover
// tiles). Call where books or their caches can change behind the loader's back.
void forgetDrawnCovers();
// A thumbnail from before gray thumbnails (1-bit) on a 16-level panel: regenerate it.
bool isOutdatedThumbnail(const Bitmap& bitmap);
std::string ensureFullCover(const std::string& bookPath, std::string* title = nullptr, std::string* author = nullptr,
                            bool* generated = nullptr);

}  // namespace BookCoverLoader
