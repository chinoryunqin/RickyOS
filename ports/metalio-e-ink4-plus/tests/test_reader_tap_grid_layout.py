"""Run the production editor geometry across target sizes; no duplicated layout."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]/'firmware/rickyos-plus-1.2/upstream'
class GridEditorLayoutTest(unittest.TestCase):
 def test_layout_bounds_and_targets(self):
  source=(ROOT/'src/activities/settings/ReaderTouchZonesActivity.cpp').read_text()
  production=source[source.index('ReaderTouchZonesActivity::Layout ReaderTouchZonesActivity::layout()'):source.index('void ReaderTouchZonesActivity::save()')]
  code=r'''
#include <algorithm>
#include <cassert>
#include <vector>
struct Rect{int x=0,y=0,width=0,height=0;};
namespace RickyDesignUi{constexpr int CAPTION=1;}
struct GfxRenderer {int w=684,h=1216;int getScreenWidth()const{return w;}int getScreenHeight()const{return h;}int getLineHeight(int)const{return 30;}};
struct Metrics{int topPadding=16,headerHeight=68;}metrics;
struct {Rect getScreenSafeArea(const GfxRenderer&r,bool,bool)const{return {4,4,r.w-8,r.h-8};}const Metrics&getMetrics()const{return metrics;}}gui;
struct UITheme{static auto&getInstance(){return gui;}};
class ReaderTouchZonesActivity{public:
 struct Layout{Rect header{},hint{},enabled{},choices[2]{},cells[9]{},grid{},reset{},done{},note{};};
 bool choosing_=false; int bank_=0; GfxRenderer renderer;Layout layout()const;
};
'''+production+r'''
int main(){
 ReaderTouchZonesActivity a;
 for(auto size:{std::pair{684,1216},std::pair{1216,684},std::pair{1000,1768},std::pair{1768,1000}}){
  for(bool choosing:{false,true})for(int bank:{0,1}){a.choosing_=choosing;a.bank_=bank;
  auto[w,h]=size;a.renderer.w=w;a.renderer.h=h;auto l=a.layout();
  std::vector<Rect> controls;
  if(choosing) controls={l.enabled,l.choices[0],l.choices[1]};
  else {
    controls={l.reset,l.done};
    assert(l.grid.x==0&&l.grid.y==0&&l.grid.width==w&&l.grid.height==h);
    int total=0;
    for(int i=0;i<9;++i){auto r=l.cells[i];assert(r.width>=44&&r.height>=44);total+=r.width*r.height;
      assert(r.x==(i%3*w+2)/3&&r.y==(i/3*h+2)/3);
      for(int dy:{0,r.height-1})for(int dx:{0,r.width-1}){
        assert((r.y+dy)*3/h*3+(r.x+dx)*3/w==i);
      }
    }
    assert(total==w*h);
  }
  for(auto r:controls){assert(r.width>=44&&r.height>=44);assert(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h);}
  assert(l.note.y+l.note.height<=h);
  for(size_t i=0;i<controls.size();++i)for(size_t j=i+1;j<controls.size();++j){
   auto x=controls[i],y=controls[j];
   assert(x.x+x.width+6<=y.x || y.x+y.width+6<=x.x || x.y+x.height+6<=y.y || y.y+y.height+6<=x.y);
  }
 }}
}
'''
  with tempfile.TemporaryDirectory(prefix='reader-grid-layout-') as d:
   p=Path(d);(p/'test.cpp').write_text(code)
   subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
