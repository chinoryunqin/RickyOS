"""Keep experimental USB MSC isolated from the shipped Read Pico profile."""

import configparser
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ReadPicoUsbProfileTest(unittest.TestCase):
    def test_usb_msc_is_trial_only(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini")
        shipped = config["env:rickyos_readpico"]
        trial = config["env:rickyos_readpico_usb"]

        self.assertNotIn("FREEINK_CAP_USB_MSC", shipped["build_flags"])
        self.assertNotIn("CONFIG_TINYUSB_ENABLED", shipped["custom_sdkconfig"])
        self.assertEqual(trial["extends"], "env:rickyos_readpico")
        self.assertIn("-DFREEINK_CAP_USB_MSC=1", trial["build_flags"])
        self.assertIn("CONFIG_TINYUSB_ENABLED=y", trial["custom_sdkconfig"])
        self.assertIn("CONFIG_TINYUSB_MSC_ENABLED=y", trial["custom_sdkconfig"])
        self.assertIn("CONFIG_TINYUSB_CDC_ENABLED=n", trial["custom_sdkconfig"])
        self.assertIn("pre:scripts/prepare_readpico_usb_component.py", trial["extra_scripts"])
        self.assertIn("-fno-lto", trial["build_unflags"])
        self.assertIn("-flto=auto", trial["build_flags"])

    def test_browser_preview_is_simulator_only(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini")
        simulator = config["env:simulator_rickyos"]
        shipped = config["env:rickyos_readpico"]
        trial = config["env:rickyos_readpico_usb"]

        self.assertIn("-DRICKYOS_USB_PREVIEW=1", simulator["build_flags"])
        self.assertNotIn("RICKYOS_USB_PREVIEW", shipped["build_flags"])
        self.assertNotIn("RICKYOS_USB_PREVIEW", trial["build_flags"])

        activity = (ROOT / "src/activities/network/UsbDriveActivity.cpp").read_text()
        self.assertIn("#if defined(RICKYOS_USB_PREVIEW)", activity)
        self.assertIn("#if !defined(RICKYOS_USB_PREVIEW)\n  if (!restartRequested) Storage.endUsbDrive();", activity)
        self.assertIn("#else\n\n  // Show the safety instructions", activity)

    def test_host_suspend_does_not_eject_mounted_disk(self):
        activity = (ROOT / "src/activities/network/UsbDriveActivity.cpp").read_text()
        self.assertNotIn("Storage.usbDriveHostSuspended()", activity)
        self.assertIn("state == State::Ejected || state == State::Disconnected", activity)

    def test_component_is_pinned_and_untracked(self):
        source = (ROOT / "scripts/prepare_readpico_usb_component.py").read_text()
        self.assertIn("LIB_BUILDER_COMMIT =", source)
        self.assertIn("TINYUSB_COMMIT =", source)
        self.assertIn("hashlib.sha256(contents).hexdigest() != digest", source)
        self.assertIn("if not verify_component(DESTINATION):", source)
        self.assertIn('PROJECT / ".pio" / "rickyos_usb_components"', source)
        self.assertIn('env.BoardConfig().update(', source)


if __name__ == "__main__":
    unittest.main()
