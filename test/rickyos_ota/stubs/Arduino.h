#pragma once
#include <cstddef>
#include <cstdint>
class Stream {
 public:
  size_t write(const uint8_t*, size_t count) { return count; }
};
