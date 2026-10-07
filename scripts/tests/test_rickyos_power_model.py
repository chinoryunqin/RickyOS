"""RickyOS side key: short press toggles Standby, a hold powers off; idle opens Standby."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]


def read(rel):
    return (ROOT / rel).read_text()


class RickyPowerModelTest(unittest.TestCase):
    def test_side_key_short_press_toggles_standby_and_hold_powers_off(self):
        main = read('src/main.cpp')
        self.assertIn('constexpr unsigned long RICKY_POWER_OFF_HOLD_MS = 2000;', main)
        block = main[main.index('// Side key: a short press toggles Standby'):]
        block = block[:block.index('#endif')]
        self.assertIn('gpio.getPowerButtonHeldTime() >= RICKY_POWER_OFF_HOLD_MS', block)
        self.assertLess(block.index('enterDeepSleep();'), block.index('activityManager.openStandby();'))
        self.assertIn('activityManager.closeStandby();', block)
        # The press that woke the device never toggles Standby.
        self.assertIn('powerReleasedSinceWake', block)
        # The generic hold-to-sleep path is off for RickyOS.
        self.assertIn('if (!kRickyPowerModel && !x4ProAwaitingClickWindow', main)

    def test_idle_opens_standby_and_standby_powers_off_after_its_time(self):
        main = read('src/main.cpp')
        idle = main[main.index('// RickyOS power model:'):]
        idle = idle[:idle.index('#else')]
        self.assertIn('activityManager.openStandby();', idle)
        self.assertNotIn('enterDeepSleep(true);\n    // This', idle)
        self.assertLess(idle.index('consumePowerOffRequest()'), idle.index('openStandby()'))
        standby = read('src/activities/apps/standby/StandbyActivity.cpp')
        self.assertIn('activityManager.requestPowerOff();', standby)
        self.assertIn('millis() - enteredMs_ >= powerOffMs', standby)
        self.assertIn('READING_STATS.resumeSession();', method(standby, 'void StandbyActivity::onExit()'))
        settings = read('src/CrossPointSettings.h')
        self.assertIn('static constexpr uint8_t RICKY_AUTO_OFF_HOURS[] = {0, 1, 3, 6, 12, 24};', settings)
        self.assertIn('uint8_t rickyAutoOffIndex = 3;  // 6 hours', settings)
        manager = read('src/activities/ActivityManager.cpp')
        open_standby = method(manager, 'void ActivityManager::openStandby()')
        self.assertIn('pushActivity(std::move(standby));', open_standby)
        self.assertIn('popActivity();', method(manager, 'void ActivityManager::closeStandby()'))

    def test_short_press_has_no_other_action(self):
        load = read('src/CrossPointSettings.cpp')
        load = load[load.index('enforceProductLayout();\n'):]
        self.assertIn('shortPwrBtn = IGNORE;', load[:400])
        page = read('src/activities/settings/SettingsActivity.cpp')
        self.assertIn('it->nameId = StrId::STR_RICKY_AUTO_STANDBY;', page)
        self.assertIn('&CrossPointSettings::rickyAutoOffIndex', page)


if __name__ == '__main__':
    unittest.main()
