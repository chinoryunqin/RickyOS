"""Execute fixed-size power windows and the production HAL sampling bridge."""
from pathlib import Path
import unittest

from test_battery_stability import ROOT, SDK, method, run_cpp

PORT = Path(__file__).resolve().parents[1]
INCLUDE = PORT / 'firmware/rickyos-plus-1.2/components/rickyos_application/port'


class PowerCaptureTest(unittest.TestCase):
    def test_windows_charge_isolation_faults_and_wrap(self):
        run_cpp(r'''
#include "PlusPowerCapture.h"
#include <cassert>
using namespace plus_board;
PowerCaptureSample sample(uint32_t ms,uint16_t q,int16_t current=-40) {
  return {ms,q,4200,current,current,0x20,1};
}
int main() {
  PowerCapture c;
  c.observe(sample(0,100,300),false); // Charging does not enter a battery window.
  assert(c.awake.samples==0 && c.cycle==0);
  c.observe(sample(1000,100),true);
  c.observe(sample(1801000,120),true);
  c.enterStandby(sample(3601000,140),true);
  assert(c.awake.state==PowerWindowState::Frozen && c.awake.faults==0);
  assert(c.awake.first.counter==100 && c.awake.last.counter==140);
  assert(!c.needsStandbyStart());
  c.exitStandby(sample(7201000,146,-6),true);
  assert(c.standby.state==PowerWindowState::Frozen && c.standby.samples==2 && c.standby.faults==0);
  assert(c.standby.last.counter-c.standby.first.counter==6);
  c.observe(sample(7231000,140,500),false); // Recharging cannot overwrite endpoints.
  c.observe(sample(7261000,135,500),false);
  assert(c.awake.last.counter==140 && c.standby.last.counter==146);
  c.observe(sample(7300000,134),true); // Next unplug starts a separate cycle.
  assert(c.cycle==2 && c.awake.samples==1 && c.standby.samples==0);

  PowerCapture late;
  late.enterStandby(sample(100,0),false);
  assert(late.needsStandbyStart());
  late.observe(sample(35000,250,-6),true); // USB removed just before sleeping.
  assert(!late.needsStandbyStart() && late.awake.samples==0);
  late.exitStandby(sample(3635000,256,-6),true);
  assert(late.standby.samples==2 && late.standby.faults==0);
  assert(late.standby.last.atMs-late.standby.first.atMs==3600000);

  PowerCapture interrupted;
  interrupted.enterStandby(sample(0,100),true);
  interrupted.exitStandby(sample(3600000,95,300),false);
  assert(interrupted.standby.faults & PowerExternalDuringStandby);
  assert(interrupted.standby.samples==1 && interrupted.standby.last.counter==100);

  PowerCaptureWindow w;
  auto bad=sample(0,100);bad.valid=0;w.observe(bad);
  assert(w.samples==0 && (w.faults & PowerReadFailed));
  w.observe(sample(1000,100));w.observe(sample(2000,0));
  assert(w.faults & PowerCounterReset);
  w={};w.observe(sample(0,100));w.observe(sample(1000,500));
  assert(w.faults & PowerCounterJump);
  w={};w.observe(sample(0,100,6));assert(w.samples==0);
  auto settling=sample(30000,100);settling.average=20;w.observe(settling);
  assert(w.samples==1 && w.faults==0); // Filter lag after unplug is not charging.
  w.observe(sample(60000,100,6));assert(w.faults & PowerCharging);
  w={};w.observe(sample(UINT32_MAX-1000,65535,-6));w.observe(sample(3598999,5,-6));
  assert(w.faults==0 && uint32_t(w.last.atMs-w.first.atMs)==3600000);
  w.freeze();w.observe(sample(7200000,10));assert(w.last.counter==5);
  PowerCaptureWindow zero;
  zero.observe(sample(0,0,0));zero.observe(sample(3600000,0,0));
  assert(zero.samples==2 && zero.faults==0); // Decode as below resolution, never proof of zero current.
}
''', INCLUDE)

    def test_hal_bridge_checks_external_power_and_gauge_readiness(self):
        source = (ROOT / 'lib/hal/HalPowerManager.cpp').read_text()
        snapshot = (ROOT / 'freeink-sdk/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp').read_text()
        body = method(snapshot, 'BatteryMonitor::PowerSnapshot BatteryMonitor::readPowerSnapshot() const')
        self.assertIn('readReg16(addr, 0x3a, s.operation)', body)
        self.assertNotIn('write', body.lower())
        run_cpp(r'''
#include "PlusPowerCapture.h"
#include <cassert>
uint32_t now=0;uint32_t millis(){return now;}
bool batteryOnly=true,plugDuringRead=false;unsigned reads=0;
namespace plus_board { bool batteryPowerOnly(){return batteryOnly;} }
struct BatteryMonitor {
  struct PowerSnapshot {
    uint16_t counter=100,mv=4200,operation=0x20;
    int16_t current=-40,average=-40;
    bool mvOk=true,currentOk=true,averageOk=true,counterOk=true,operationOk=true;
  };
  static PowerSnapshot raw;
  PowerSnapshot readPowerSnapshot()const{++reads;if(plugDuringRead)batteryOnly=false;return raw;}
};
BatteryMonitor::PowerSnapshot BatteryMonitor::raw;
''' + method(source, 'plus_board::PowerCaptureSample readPowerCaptureSample(bool& batteryOnly)') + r'''
int main(){
 bool battery=false;now=1000;
 auto s=readPowerCaptureSample(battery);assert(battery&&s.valid&&s.atMs==1000&&reads==1);
 batteryOnly=false;s=readPowerCaptureSample(battery);assert(!battery&&!s.valid&&reads==1);
 batteryOnly=true;plugDuringRead=true;s=readPowerCaptureSample(battery);assert(!battery&&reads==2);
 plugDuringRead=false;batteryOnly=true;BatteryMonitor::raw.operation=0;
 s=readPowerCaptureSample(battery);assert(battery&&!s.valid);
 BatteryMonitor::raw.operation=0x420;s=readPowerCaptureSample(battery);assert(!s.valid);
 BatteryMonitor::raw.operation=0x20;BatteryMonitor::raw.counterOk=false;
 s=readPowerCaptureSample(battery);assert(!s.valid);
 BatteryMonitor::raw.counterOk=true;BatteryMonitor::raw.mv=0;
 s=readPowerCaptureSample(battery);assert(!s.valid);
}
''', INCLUDE)

    def test_capture_does_not_add_sleep_wakes_or_replace_existing_poll(self):
        source = (ROOT / 'lib/hal/HalPowerManager.cpp').read_text()
        poll = method(source, 'void HalPowerManager::pollBatteryWhileAwake()')
        self.assertEqual(poll.count('getBatteryPercentage();'), 1)
        self.assertEqual(poll.count('observePowerCapture();'), 1)
        self.assertLess(poll.index('< 30000'), poll.index('observePowerCapture();'))
        light = method(source, 'HalPowerManager::LightSleepWakeReason HalPowerManager::lightSleepFor(const uint32_t seconds) const')
        self.assertIn('if (g_plusPowerCapture.needsStandbyStart()) observePowerCapture();', light)
        helper = method(source, 'void observePowerCapture()')
        self.assertNotIn('sleep', helper.lower())
        self.assertNotIn('delay(', helper)

    def test_production_poll_and_activity_boundaries_freeze_before_usb(self):
        source = (ROOT / 'lib/hal/HalPowerManager.cpp').read_text()
        code = r'''
#include "PlusPowerCapture.h"
#include "PlusBoard.h"
#include <cassert>
#define FREEINK_DEVICE_METALIO_PLUS 1
#define FREEINK_DEVICE_READPICO 0
#define RICKYOS_PLUS_POWER_CAPTURE 1
template<class... T> void logIgnore(T...){}
#define LOG_INF(...) logIgnore(__VA_ARGS__)
uint32_t now=0;uint32_t millis(){return now;}
bool batteryOnly=false;unsigned reads=0,polls=0;
namespace plus_board {
 bool batteryPowerOnly(){return batteryOnly;}
 void beginStandbyPowerTrace(){}
 StandbyPowerSnapshot standbyPowerSnapshot(){return {};}
}
struct BatteryMonitor {
 struct PowerSnapshot {
  uint16_t counter=100,mv=4200,operation=0x20;
  int16_t current=-40,average=-40;
  bool mvOk=true,currentOk=true,averageOk=true,counterOk=true,operationOk=true;
 };
 static PowerSnapshot raw;
 PowerSnapshot readPowerSnapshot() const {++reads;return raw;}
};
BatteryMonitor::PowerSnapshot BatteryMonitor::raw;
plus_board::PowerCapture g_plusPowerCapture;
bool g_haveStandbySummary=false;
plus_board::StandbyPowerSnapshot g_lastStandbySummary{};
void logStandbySnapshot(plus_board::StandbyPowerSnapshot){}
struct HalPowerManager {
 uint32_t batteryAwakePollAt=0;bool batteryAwakePolled=false;
 void pollBatteryWhileAwake();
 void getBatteryPercentage(){++polls;}
 static void beginStandbyKeyWatch();
 static void logStandbySleepSummary();
};
'''
        for signature in ['void logPowerCaptureWindow(', 'plus_board::PowerCaptureSample readPowerCaptureSample(',
                          'void observePowerCapture()', 'void HalPowerManager::pollBatteryWhileAwake()',
                          'void HalPowerManager::beginStandbyKeyWatch()', 'void HalPowerManager::logStandbySleepSummary()']:
            code += method(source, signature) + '\n'
        code += r'''
int main(){
 HalPowerManager hal;hal.pollBatteryWhileAwake();assert(reads==0&&polls==1);
 batteryOnly=true;now=30000;hal.pollBatteryWhileAwake();assert(reads==1&&polls==2);
 for(unsigned i=0;i<100;++i)hal.pollBatteryWhileAwake();assert(reads==1&&polls==2);
 now=3630000;BatteryMonitor::raw.counter=140;
 HalPowerManager::beginStandbyKeyWatch();assert(reads==2);
 assert(g_plusPowerCapture.awake.state==plus_board::PowerWindowState::Frozen);
 // No calls during the blocked sleep interval; only exit captures its endpoint.
 now=7230000;BatteryMonitor::raw.counter=146;
 HalPowerManager::logStandbySleepSummary();assert(reads==3&&g_haveStandbySummary);
 assert(g_plusPowerCapture.standby.samples==2);
 assert(g_plusPowerCapture.standby.last.counter==146);
 batteryOnly=false;BatteryMonitor::raw.counter=140;
 now+=30000;hal.pollBatteryWhileAwake();assert(reads==3&&polls==3);
 assert(g_plusPowerCapture.awake.last.counter==140&&g_plusPowerCapture.standby.last.counter==146);
}
'''
        run_cpp(code, INCLUDE)

    def test_reader_requires_frozen_valid_long_windows(self):
        import importlib.util
        import struct
        script = PORT / 'scripts/read_plus_power_capture.py'
        spec = importlib.util.spec_from_file_location('power_reader', script)
        reader = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reader)

        def window(delta, faults=0, state=2, duration=3600000):
            first = struct.pack('<IHHhhHH', 1000, 100, 4200, -40, -40, 0x20, 1)
            last = struct.pack('<IHHhhHH', 1000+duration, 100+delta, 4150, -6, -6, 0x20, 1)
            return first+last+struct.pack('<III', 2, faults, state)

        raw = struct.pack('<5I', 0x504f5752, 1, 1, 0, 1) + window(40) + window(6)
        result = reader.decode_capture(struct.unpack('<27I', raw))
        self.assertEqual(result['awake']['indicated_average_discharge_ma'], 40)
        self.assertEqual(result['standby']['indicated_average_discharge_ma'], 6)
        self.assertFalse(reader.decode_window(window(6, faults=16), 1800)['valid'])
        self.assertFalse(reader.decode_window(window(6, state=1), 1800)['valid'])
        self.assertFalse(reader.decode_window(window(1, duration=120000), 1800)['valid'])
        zero = reader.decode_window(window(0), 1800)
        self.assertTrue(zero['valid'])
        self.assertIsNone(zero['indicated_average_discharge_ma'])
        self.assertEqual(zero['classification'], 'below_counter_resolution')
        short = struct.pack('<5I', 0x504f5752, 1, 1, 0, 1) + window(1, duration=90000) + window(3, duration=1770000)
        quick = reader.decode_capture(struct.unpack('<27I', short))
        self.assertTrue(quick['standby']['valid'])
        self.assertFalse(quick['awake']['valid'])
        self.assertNotIn('indicated_average_discharge_ma', quick['awake'])
        self.assertEqual(quick['awake_current_reference']['filtered_current_ma'], -6)
        self.assertEqual(quick['awake_current_reference']['kind'], 'point_and_filtered_readings_only')
        faulty = struct.pack('<5I', 0x504f5752, 1, 1, 0, 1) + window(1, faults=1, duration=90000) + window(3, duration=1770000)
        self.assertNotIn('awake_current_reference', reader.decode_capture(struct.unpack('<27I', faulty)))
        self.assertEqual(reader.symbol_address('  0x2f001234 _ZNfoo_g_plusPowerCaptureE.lto_priv.0\n',
                                              'g_plusPowerCapture'), 0x2f001234)


if __name__ == '__main__':
    unittest.main()
