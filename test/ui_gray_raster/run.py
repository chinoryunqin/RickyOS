"""Exercise production four-tone conversion independently of the UI renderer."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = r'''
#include "UiGrayRaster.h"
#include "UiSceneFit.h"
#include <cassert>
#include <cstring>
using namespace uiGrayRaster;
int main() {
  const uint8_t ramp[]{0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
  const uint8_t golden[3][2]{{0x00, 0x07}, {0x1F, 0x00}, {0x1F, 0xF8}};
  const Plane planes[]{Plane::Base, Plane::Lsb, Plane::Msb};
  for (unsigned p=0; p<3; ++p) {
    uint8_t out[]{0xA5, 0xCC, 0xCC, 0x5A};
    row(ramp, out+1, 2, planes[p]);
    assert(out[0]==0xA5 && out[3]==0x5A);
    assert(!memcmp(out+1, golden[p], 2));
  }
  for (uint8_t level=0; level<16; ++level) {
    uint8_t flat[400]; memset(flat, level*17, sizeof(flat));
    uint8_t out[100];
    for (auto plane : planes) {
      row(flat, out, 100, plane);
      for (auto byte : out) assert(byte==out[0] && (byte==0 || byte==255));
    }
  }
  // Zero rows may use null pointers and must perform no access.
  row(nullptr, nullptr, 0, Plane::Base);
  {
    const uiSceneFit::Rect source{40,144,604,960};
    const auto target=uiSceneFit::fit(source,{40,64,400,620});
    assert(target.x==45 && target.y==64 && target.width==390 && target.height==620);
    int x=target.x,y=target.y;
    assert(uiSceneFit::inverse(source,target,x,y) && x==40 && y==144);
    x=target.x+target.width-1;y=target.y+target.height-1;
    assert(uiSceneFit::inverse(source,target,x,y) && x<644 && y<1104);
    x=target.x-1;y=target.y;
    assert(!uiSceneFit::inverse(source,target,x,y));
    uint8_t checker[32]{};
    for(int sx=0;sx<8;++sx) for(int sy=0;sy<8;++sy) {
      const int index=(7-sx)*4+sy/2,shift=(sy&1)*4;
      const int level=(sx+sy)&1 ? 15:0;
      checker[index]=(checker[index]&~(15<<shift))|(level<<shift);
    }
    for(int dx=0;dx<4;++dx) for(int dy=0;dy<4;++dy)
      assert(uiSceneFit::sample(checker,8,8,{0,0,8,8},{0,0,4,4},dx,dy)==8);
    memset(checker,0x55,sizeof(checker));
    for(int dx=0;dx<3;++dx) for(int dy=0;dy<5;++dy)
      assert(uiSceneFit::sample(checker,8,8,{0,0,8,8},{0,0,3,5},dx,dy)==5);
    assert(uiSceneFit::sample(nullptr,8,8,{0,0,8,8},{0,0,4,4},0,0)==15);
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    file = Path(directory) / "test.cpp"
    file.write_text(source)
    binary = Path(directory) / "test"
    subprocess.run(["c++", "-std=c++17", "-fsanitize=address,undefined", "-I"+str(root/"lib/hal"),
                    "-I"+str(root/"lib/GfxRenderer"),
                    str(file), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("UI gray: ramp/polarity, bounds, flat-tone/no-dither and empty row passed")
