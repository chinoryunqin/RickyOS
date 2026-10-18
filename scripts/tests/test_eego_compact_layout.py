"""A4 page geometry stays on paper in both orientations, including larger metrics."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class EegoLayoutTest(unittest.TestCase):
    def test_actual_geometry_bounds_gaps_and_reachable_settings(self):
        program = r'''
#include <cassert>
#include <array>
#include "RickyCompactLayout.h"
namespace c=RickyCompactLayout;
using R=c::fui::Rect;
bool overlaps(R a,R b) {
 return a.x<b.right() && b.x<a.right() && a.y<b.bottom() && b.y<a.bottom();
}
template<size_t N> void check(R paper,const std::array<R,N>& regions) {
 for(auto r:regions) {
  assert(r.width>0 && r.height>0);
  assert(r.x>=paper.x && r.y>=paper.y && r.right()<=paper.right() && r.bottom()<=paper.bottom());
 }
 for(size_t i=0;i<N;++i) for(size_t j=i+1;j<N;++j) assert(!overlaps(regions[i],regions[j]));
}
int main() {
 for(R bounds: {R{24,4,504,660},R{48,28,456,624},R{24,4,720,444},R{48,28,672,408}}) {
  for(int small: {28,32,36}) {
   const auto h=c::home(bounds,44,36,small);
   check(bounds,std::array{h.profile,h.mascot,h.main,h.stats,h.motto});
   assert(h.main.height>=200 && h.mascot.height==90);
   const auto s=c::settings(bounds,44,36,small);
   check(bounds,std::array{s.title,s.profile,s.grid,s.about,s.help});
   const int rows=6/s.columns;
   const int cardHeight=(s.grid.height-c::gap*(rows-1))/rows;
   assert(cardHeight>=72); // icon and label stay together inside each drawn/hit card
   assert(s.about.height>=44 && s.help.height>=44);
  }
 }
}
'''
        with tempfile.TemporaryDirectory(prefix='eego-layout-') as folder:
            source=Path(folder)/'test.cpp';binary=Path(folder)/'test'
            source.write_text(program)
            subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                '-I'+str(ROOT/'src/components'),
                '-I'+str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include'),
                str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
