#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "RickyWebpMemory.h"
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif
namespace {
struct alignas(std::max_align_t) Header {
  size_t bytes;
};
std::atomic_flag busy = ATOMIC_FLAG_INIT;
size_t live = 0, peak = 0;
bool failed = false;
constexpr size_t kBudget = 8 * 1024 * 1024;
constexpr size_t kReserve = 512 * 1024;
}  // namespace
extern "C" int RickyWebpBegin() {
  if (busy.test_and_set(std::memory_order_acquire)) return 0;
  if (live) {
    busy.clear(std::memory_order_release);
    return 0;
  }
  peak = 0;
  failed = false;
  return 1;
}
extern "C" void RickyWebpEnd() { busy.clear(std::memory_order_release); }
extern "C" size_t RickyWebpLive() { return live; }
extern "C" size_t RickyWebpPeak() { return peak; }
extern "C" int RickyWebpAllocationFailed() { return failed; }
extern "C" void* RickyWebpMalloc(size_t n) {
  if (!n || n > SIZE_MAX - sizeof(Header)) {
    failed = true;
    return nullptr;
  }
  const size_t total = n + sizeof(Header);
  if (total > kBudget || live > kBudget - total) {
    failed = true;
    return nullptr;
  }
#ifdef ESP_PLATFORM
  const auto free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if (free < n + sizeof(Header) + kReserve) {
    failed = true;
    return nullptr;
  }
  auto* h = static_cast<Header*>(heap_caps_aligned_alloc(alignof(Header), total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  auto* h = static_cast<Header*>(std::malloc(n + sizeof(Header)));
#endif
  if (!h) {
    failed = true;
    return nullptr;
  }
  h->bytes = total;
  live += total;
  if (live > peak) peak = live;
  return h + 1;
}
extern "C" void* RickyWebpCalloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size) {
    failed = true;
    return nullptr;
  }
  void* p = RickyWebpMalloc(n * size);
  if (p) std::memset(p, 0, n * size);
  return p;
}
extern "C" void RickyWebpFree(void* p) {
  if (!p) return;
  auto* h = static_cast<Header*>(p) - 1;
  live -= h->bytes;
#ifdef ESP_PLATFORM
  heap_caps_free(h);
#else
  std::free(h);
#endif
}
