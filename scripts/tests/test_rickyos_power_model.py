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
        self.assertIn('now - pressedAt >= RICKY_POWER_OFF_HOLD_MS', block)
        self.assertLess(block.index('enterDeepSleep();'), block.index('activityManager.openStandby();'))
        self.assertIn('activityManager.closeStandby();', block)
        # A failed PMU poll reads as released: only a release lasting 200 ms counts, so a
        # hold is never split into short presses (measured on the device: it toggled Standby).
        self.assertIn('constexpr unsigned long kSideKeyReleaseMs = 200;', block)
        self.assertIn('now - lastDownAt >= kSideKeyReleaseMs', block)
        # The press that powered the device on never toggles Standby or powers off.
        self.assertIn('wakePressPending', block)
        self.assertIn('!wakePressPending && !holdHandled', block)
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

    def test_side_key_wakes_standby_light_sleep(self):
        # The PMU holds its interrupt while key events are queued; unacknowledged, a
        # full FIFO meant no press could end Standby's light sleep (device: only a
        # hold worked once Standby had slept). Drain before and after every sleep.
        import sys
        sys.path.insert(0, str(ROOT / 'scripts'))
        import patch_rickyos_pmu_keys as patch
        self.assertIn('pre:scripts/patch_rickyos_pmu_keys.py', read('platformio.ini'))
        board = read('freeink-sdk/libs/hardware/BoardReadPico/src/BoardReadPico.cpp')
        header = read('freeink-sdk/libs/hardware/BoardReadPico/include/BoardReadPico.h')
        for source, old, new in ((header, patch.OLD_DECL, patch.NEW_DECL), (board, patch.OLD_DEF, patch.NEW_DEF)):
            patched = patch.patch_text(source, old, new)
            self.assertIn(new, patched)
            self.assertEqual(patched, patch.patch_text(patched, old, new))
        self.assertIn('if (!pmuEventAck(id)) break;', patch.NEW_DEF)
        hal = read('lib/hal/HalPowerManager.cpp')
        sleep = hal[hal.index('HalPowerManager::LightSleepWakeReason lightSleepReadPico('):]
        self.assertLess(sleep.index('takePmuKeyPress(keyDown);'), sleep.index('esp_light_sleep_start()'))
        self.assertLess(sleep.index('esp_light_sleep_start()'), sleep.index('if (BoardReadPico::takePmuKeyPress(keyDown))'))
        standby = method(read('src/activities/apps/standby/StandbyActivity.cpp'),
                         'bool StandbyActivity::tryLightSleep(')
        quick = standby[standby.index('LightSleepWakeReason::PowerButton:'):standby.index('LightSleepWakeReason::PowerButtonHeld:')]
        self.assertIn('activityManager.closeStandby();', quick)
        main = read('src/main.cpp')
        self.assertIn('if (standbyWasShowing && !standbyShowingNow) lastActivityTime = millis();', main)

if __name__ == '__main__':
    unittest.main()
