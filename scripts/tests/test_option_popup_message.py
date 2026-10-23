"""Check the actual popup geometry and shared draw/touch message wiring."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PopupMessageTest(unittest.TestCase):
    def test_production_geometry(self):
        source = (ROOT / 'src/InxItemLayout.h').read_text()
        start = source.index('namespace InxOptionGeometry {')
        end = source.index('}  // namespace InxOptionGeometry', start)
        geometry = source[start:end] + '}\n'
        for high_dpi in (False, True):
            program = '''
#include <algorithm>
#include <cassert>
struct Rect { int x, y, width, height; };
struct UiHighDpiProfile {
  static constexpr bool enabled = HIGH_DPI;
  static constexpr int buttonHeight = 72;
};
'''.replace('HIGH_DPI', str(high_dpi).lower()) + geometry + r'''
int main() {
  using namespace InxOptionGeometry;
  Rect bounds{32, 80, 620, 1000};
  for (int count = 1; count <= 12; ++count) {
    for (int selected = 0; selected < count; ++selected) {
      auto plain = layout(bounds, count, selected, false);
      auto message = layout(bounds, count, selected, true);
      assert(message.header == plain.header + messageHeight);
      assert(message.rows == plain.rows && message.first == plain.first);
      assert(message.panel.height == plain.panel.height + messageHeight);
      assert(message.panel.y >= bounds.y);
      assert(message.panel.y + message.panel.height <= bounds.y + bounds.height);
      for (int slot = 0; slot < message.rows; ++slot) {
        auto hit = message.optionRect(slot);
        assert(hit.y >= message.panel.y + message.header);
        assert(hit.y + hit.height <= message.panel.y + message.panel.height);
        if (slot + 1 < message.rows) {
          auto next = message.optionRect(slot + 1);
          assert(next.y - hit.y - hit.height >= 6);
        }
      }
    }
  }
}
'''
            with tempfile.TemporaryDirectory() as tmp:
                file = Path(tmp) / 'test.cpp'
                file.write_text(program)
                exe = Path(tmp) / 'test'
                subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', str(file), '-o', str(exe)], check=True)
                subprocess.run([str(exe)], check=True)

    def test_body_passed_and_touch_uses_same_message_geometry(self):
        popup = (ROOT / 'src/components/OptionPopup.h').read_text()
        theme = (ROOT / 'src/components/themes/inx/InxTheme.cpp').read_text()
        self.assertIn('optionCount, selectedIndex, !message.empty()', popup)
        self.assertIn('message.empty() ? nullptr : message.c_str()', popup)
        self.assertIn('optionCount, selected, hasMessage', theme)
        self.assertIn('message, RickyDesignUi::BODY', theme)
        self.assertIn('panelY + layout.header + slot', theme)


if __name__ == '__main__':
    unittest.main()
