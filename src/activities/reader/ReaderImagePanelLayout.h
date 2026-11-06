#pragma once
#include <algorithm>

// One geometry source for paint and touch, independent of board resolution.
namespace readerImagePanel {
struct Box {
  int x = 0, y = 0, width = 0, height = 0;
  int right() const { return x + width; }
  int bottom() const { return y + height; }
};
struct Layout {
  Box preview, compare, next, presets[4], contrast, brightness, smooth[2], scope, cancel, apply;
  int gap = 8;
  int control = 44;
};
inline Box column(const Box& band, int index, int count, int gap) {
  const int width = (band.width - (count - 1) * gap) / count;
  return {band.x + index * (width + gap), band.y, width, band.height};
}
inline Layout layout(Box body, int caption, int desiredControl, int gap) {
  Layout out;
  out.gap = std::max(6, gap);
  gap = out.gap;
  const bool landscape = body.width > body.height;
  // Seven control bands, two captions and their gaps. Shrink the control band
  // before sacrificing the image; minTouchSize is exactly the painted band.
  const int available = landscape ? body.height : body.height * 3 / 4;
  const int fit = landscape ? (available - caption - 6 * gap) / 5 : (available - 2 * caption - 9 * gap) / 7;
  const int control = std::max(44, std::min(desiredControl, fit));
  out.control = control;
  const int toneHeight = caption + gap + control;
  Box actions{body.x, body.bottom() - control, body.width, control};
  out.cancel = column(actions, 0, 2, gap);
  out.apply = column(actions, 1, 2, gap);
  body.height -= control + gap;
  Box controls = body;
  if (landscape) {
    const int previewWidth = (body.width - gap) * 44 / 100;
    out.preview = {body.x, body.y, previewWidth, body.height - control - gap};
    controls.x += previewWidth + gap;
    controls.width -= previewWidth + gap;
  } else {
    const int controlsHeight = 5 * control + 2 * caption + 6 * gap;
    out.preview = {body.x, body.y, body.width, body.height - controlsHeight - control - 2 * gap};
    controls.y = out.preview.bottom() + control + 2 * gap;
    controls.height = controlsHeight;
  }
  Box compareBand{out.preview.x, out.preview.bottom() + gap, out.preview.width, control};
  out.compare = column(compareBand, 0, 2, gap);
  out.next = column(compareBand, 1, 2, gap);
  Box presetBand{controls.x, controls.y, controls.width, control};
  for (int i = 0; i < 4; ++i) out.presets[i] = column(presetBand, i, 4, gap);
  const Box toneBand{controls.x, presetBand.bottom() + gap, controls.width, toneHeight};
  out.contrast = landscape ? column(toneBand, 0, 2, gap) : toneBand;
  out.brightness = landscape ? column(toneBand, 1, 2, gap)
                            : Box{controls.x, out.contrast.bottom() + gap, controls.width, toneHeight};
  Box smoothBand{controls.x, out.brightness.bottom() + gap, controls.width, control};
  out.smooth[0] = column(smoothBand, 0, 2, gap);
  out.smooth[1] = column(smoothBand, 1, 2, gap);
  out.scope = {controls.x, smoothBand.bottom() + gap, controls.width, control};
  return out;
}
inline int toneFromPermille(int permille, int limit) {
  const int steps = (std::clamp(permille, 0, 1000) * (2 * limit / 5) + 500) / 1000;
  return -limit + steps * 5;
}
}  // namespace readerImagePanel
