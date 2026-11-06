#pragma once

#include <atomic>
#include <string>

class Bitmap;

namespace BookCoverLoader {

std::string ensureThumbnail(const std::string& bookPath, int height, bool* generated = nullptr);
// The file ensureThumbnail() would return for a supported EPUB/MOBI/AZW3,
// without touching the SD card; empty for other formats.
std::string thumbnailPath(const std::string& bookPath, int height);
// False when ensureThumbnail() cannot make anything: unsupported format, or already known in
// this session to have no usable cover. No SD access.
bool canMakeThumbnail(const std::string& bookPath, int height);
// Drop what this process remembers about covers (books without one, drawn cover
// tiles). Call where books or their caches can change behind the loader's back.
void forgetDrawnCovers();
// A thumbnail from before gray thumbnails (1-bit) on a 16-level panel: regenerate it.
bool isOutdatedThumbnail(const Bitmap& bitmap);
std::string ensureFullCover(const std::string& bookPath, std::string* title = nullptr, std::string* author = nullptr,
                            bool* generated = nullptr);

// One job per active shelf. The worker owns no renderer state and publishes
// the result only after all thumbnail files are closed.
class ThumbnailJob {
 public:
  bool start(const std::string& book, int height);
  bool running() const { return running_.load(std::memory_order_acquire); }
  bool isWriting(const std::string& book, int height) const;
  bool made() const { return made_; }  // only after running() is false
  void wait() const;

 private:
  static void worker(void* context);
  std::string book_;
  int height_ = 0;
  std::atomic<bool> running_{false};
  bool made_ = false;
};

}  // namespace BookCoverLoader
