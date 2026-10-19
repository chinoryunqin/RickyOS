#pragma once

#include <algorithm>
#include <cstdlib>

// Track the original contact, not just the driver's release classification.
// Fixed inline state; no allocation or additional touch polling.
class ControlCenterPull {
 public:
  void begin(int x, int y, int topBand, int screenHeight, int orientation) {
    startX_ = lastX_ = x;
    startY_ = lastY_ = peakY_ = y;
    orientation_ = orientation;
    minimumTravel_ = std::max(60, screenHeight / 12);
    reverseSlop_ = std::max(12, minimumTravel_ / 3);
    tracking_ = true;
    armed_ = y >= 0 && y < topBand;
  }

  void move(int x, int y, int orientation) {
    if (!tracking_) return;
    lastX_ = x;
    lastY_ = y;
    if (orientation != orientation_ || y < peakY_ - reverseSlop_) armed_ = false;
    peakY_ = std::max(peakY_, y);
  }

  bool release(int orientation) {
    const int dy = lastY_ - startY_;
    const bool open =
        tracking_ && armed_ && orientation == orientation_ && dy >= minimumTravel_ && dy > std::abs(lastX_ - startX_);
    reset();
    return open;
  }

  void reset() { tracking_ = armed_ = false; }

 private:
  int startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0, peakY_ = 0;
  int orientation_ = 0, minimumTravel_ = 60, reverseSlop_ = 20;
  bool tracking_ = false, armed_ = false;
};
