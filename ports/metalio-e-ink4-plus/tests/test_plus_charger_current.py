"""Run the real Plus charger configuration against a byte-addressed I2C model."""
from pathlib import Path
import subprocess
import sys
import tempfile

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT.parents[1] / 'scripts/tests'))
from test_reading_ui_regressions import method

SOURCE = PORT / 'firmware/rickyos-plus-1.2/components/rickyos_application/port/PlusBoard.cpp'
PROGRAM = r'''
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
std::array<uint8_t,256> registers, initial;
bool locked=false, lockOk=true, probeOk=true;
int writes=0, failReg=-1; bool failWrite=false;
bool metalio_i2c1_lock(int){if(!lockOk)return false;assert(!locked);locked=true;return true;}
void metalio_i2c1_unlock(){assert(locked);locked=false;}
int plus_i2c_probe(uint8_t a){assert(locked&&a==0x6b);return probeOk?0:-1;}
int plus_i2c_read(uint8_t a,uint8_t r,uint8_t* p,size_t n){
 assert(locked&&a==0x6b&&n==1);if(r==failReg&&!failWrite)return -1;
 *p=r==0x70?0x03:registers[r];return 0;
}
int plus_i2c_write(uint8_t a,uint8_t r,const uint8_t* p,size_t n){
 assert(locked&&a==0x6b&&n==1);if(r==failReg&&failWrite)return -1;
 registers[r]=*p;++writes;return 0;
}
void reset(uint8_t seed){
 for(size_t i=0;i<registers.size();++i)registers[i]=static_cast<uint8_t>(i*31+seed);
 initial=registers;locked=false;lockOk=true;probeOk=true;writes=0;failReg=-1;failWrite=false;
}
'''
MAIN = r'''
int main(){
 for(int seed=0;seed<256;++seed){
  reset(seed);assert(configureCharger());assert(!locked);
  // OEM 1.5 A option: ICHG uses 80 mA steps, IINDPM uses 20 mA steps.
  const unsigned charge=(registers[2]>>6)|((registers[3]&15)<<2);
  const unsigned input=(registers[6]>>4)|((registers[7]&15)<<4);
  assert(charge==18&&charge*80==1440);assert(input==72&&input*20==1440);
  assert((registers[2]&63)==(initial[2]&63));assert((registers[3]&240)==(initial[3]&240));
  assert((registers[6]&15)==(initial[6]&15));assert((registers[7]&240)==(initial[7]&240));
  const unsigned voltage=((registers[4]>>3)&31)|((registers[5]&15)<<5);
  assert(voltage*10==4400);assert((registers[0x16]&0x20)!=0);
 }
 reset(0);lockOk=false;assert(!configureCharger()&&!locked&&writes==0);
 reset(0);probeOk=false;assert(!configureCharger()&&!locked&&writes==0);
 for(int reg:{2,3,6,7})for(bool writing:{false,true}){
  reset(171);failReg=reg;failWrite=writing;assert(!configureCharger());assert(!locked);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='plus-charger-') as directory:
    root = Path(directory)
    code = root / 'test.cpp'
    code.write_text(PROGRAM + method(SOURCE.read_text(), 'bool configureCharger()') + MAIN)
    exe = root / 'check'
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Plus 1.5 A: real charger writes, 256 initial bit patterns, 1440 mA charge/input, 4400 mV, lock/probe and 8 I2C failures passed (ASan/UBSan).')
