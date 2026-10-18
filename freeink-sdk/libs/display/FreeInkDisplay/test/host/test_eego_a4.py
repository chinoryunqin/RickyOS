"""Compile the production A4 driver against a recording bus, including OOM."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix='eego-driver-') as folder:
    root = Path(folder)
    for part in ('src/driver', 'src/bus', 'src/lut', 'include'):
        (root / part).mkdir(parents=True)
    for name in ('PanelDriver.h', 'Uc8279cA4Driver.h', 'Uc8279cA4Driver.cpp'):
        shutil.copy2(LIB / 'src/driver' / name, root / 'src/driver' / name)
    shutil.copy2(LIB / 'src/lut/Uc8279cA4Luts.h', root / 'src/lut/Uc8279cA4Luts.h')
    for name in ('GrayscaleCapabilities.h', 'RefreshContext.h'):
        shutil.copy2(LIB / 'include' / name, root / 'include' / name)
    (root / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#define HIGH 1
#define LOW 0
#define PROGMEM
inline void digitalWrite(int,int) {}
inline void delay(unsigned long) {}
''')
    (root / 'BoardConfig.h').write_text('''#pragma once
namespace BoardConfig {
inline struct { unsigned short displayWidth=768, displayHeight=552; unsigned displaySpiHz=20000000; } ACTIVE;
}
''')
    (root / 'esp_heap_caps.h').write_text('''#pragma once
#include <cstdlib>
#include <cassert>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline bool failPsram=false;
inline void* heap_caps_malloc(size_t n,int caps) {
  assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  return failPsram ? nullptr : std::malloc(n);
}
''')
    (root / 'src/bus/EpdBus.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace freeink {
enum class BusyPolarity { ActiveLow };
struct Write { uint8_t command; std::vector<uint8_t> bytes; };
class EpdBus {
public:
  std::vector<Write> log;
  void cmd(uint8_t c) { log.push_back({c,{}}); }
  void data(const uint8_t* p,size_t n) { log.back().bytes.insert(log.back().bytes.end(),p,p+n); }
  void cmdData(uint8_t c,const uint8_t* p,size_t n) {cmd(c);data(p,n);}
  void rawWriteBytes(const uint8_t* p,size_t n) {data(p,n);}
  void beginTxn() {}
  void endTxn() {}
  void fillPlane(uint8_t c,uint8_t v,uint16_t h,uint16_t wb) {cmd(c);log.back().bytes.assign(size_t(h)*wb,v);}
  struct Pins {int8_t rst=13;};
  Pins pins() const {return {};}
  void waitBusy(const char*) {}
};
}
''')
    test=root/'test.cpp'
    test.write_text(r'''
#include <cassert>
#include <vector>
#include "esp_heap_caps.h"
#include "src/driver/Uc8279cA4Driver.h"
using namespace freeink;
std::vector<const Write*> writes(const EpdBus& b,uint8_t command) {
  std::vector<const Write*> result;
  for(const auto& w:b.log) if(w.command==command) result.push_back(&w);
  return result;
}
int main() {
  EpdBus bus;
  Uc8279cA4Driver driver;
  assert(driver.geometry().bufferSize==52992);
  std::vector<uint8_t> bw(52992,0x55),lsb(52992,0xaa),msb(52992,0xcc);
  bw[551*96]=0xab;
  driver.begin(bus);bus.log.clear();
  driver.display(bus,bw.data(),nullptr,RefreshMode::Fast,true);
  assert(writes(bus,0x20).back()->bytes.size()==49); // first frame clears
  auto old=writes(bus,0x10),next=writes(bus,0x13);
  assert(old.size()==2 && next.size()==1);
  assert(old[0]->bytes[0]==uint8_t(~0xab) && old[1]->bytes[0]==0xab);
  assert(next[0]->bytes.size()==600*96 && next[0]->bytes[0]==0xab);
  for(size_t i=552*96;i<600*96;++i) assert(next[0]->bytes[i]==0xff);
  for(int i=0;i<5;++i) {
    bus.log.clear();driver.display(bus,bw.data(),nullptr,RefreshMode::Fast,true);
    assert(writes(bus,0x20).back()->bytes.size()==(i<4?42u:49u));
  }
  driver.copyGrayscaleLsb(bus,lsb.data());driver.copyGrayscaleMsb(bus,msb.data());
  bus.log.clear();driver.displayGray(bus,bw.data(),true,nullptr,false);
  assert(writes(bus,0x01).back()->bytes[0]==0x43);
  assert(writes(bus,0x82).back()->bytes[0]==0x24);
  assert(writes(bus,0x10).back()->bytes[0]==0x55); // complemented LSB, never MSB
  assert(writes(bus,0x13).back()->bytes[0]==0x33);
  bus.log.clear();driver.display(bus,bw.data(),nullptr,RefreshMode::Fast,true);
  old=writes(bus,0x10);
  assert(old.size()==3); // init seed, inverse target, synchronized baseline
  assert(old[1]->bytes[0]==uint8_t(~0xab) && old[2]->bytes[0]==0xab);
  assert(writes(bus,0x20).back()->bytes.size()==42);
  size_t refresh=0,sync=0,powerOff=0;
  for(size_t i=0;i<bus.log.size();++i) {
    if(bus.log[i].command==0x12) refresh=i;
    if(bus.log[i].command==0x10) sync=i;
    if(bus.log[i].command==0x02) powerOff=i;
  }
  assert(refresh<sync && sync<powerOff);
  driver.deepSleep(bus);driver.begin(bus);bus.log.clear();
  driver.display(bus,bw.data(),nullptr,RefreshMode::Fast,true);
  assert(writes(bus,0x20).back()->bytes.size()==49);
  failPsram=true;
  Uc8279cA4Driver oom;
  bus.log.clear();oom.copyGrayscaleLsb(bus,lsb.data());oom.copyGrayscaleMsb(bus,msb.data());
  oom.displayGray(bus,bw.data(),true,nullptr,false);
  assert(bus.log.empty()); // failed optional gray allocation never submits a frame
}
''')
    binary=root/'test'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-I'+str(root),'-I'+str(root/'include'),str(test),
                    str(root/'src/driver/Uc8279cA4Driver.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('A4 driver: planes, padding, drive registers, baseline, cadence, wake and PSRAM OOM passed')
