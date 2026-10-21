#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "HalStorage.h"

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (fake::fail(fake::failAlloc)) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (fake::fail(fake::failAlloc)) return nullptr;
  return std::make_unique<T>(count);
}

#include <cstdint>
#include <cstdlib>

// The host has no PSRAM: buffers that would live there are unavailable, so callers
// exercise their file-backed paths.
namespace memory {
struct FreeDeleter final {
  void operator()(uint8_t* ptr) const noexcept { std::free(ptr); }
};
using ByteBuffer = std::unique_ptr<uint8_t, FreeDeleter>;
inline ByteBuffer makePsramByteBufferUninitializedNoThrow(size_t) { return {}; }
inline bool psramHasHeadroom(size_t, size_t, size_t) { return false; }
}  // namespace memory
