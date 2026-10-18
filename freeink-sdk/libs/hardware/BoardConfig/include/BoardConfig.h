#pragma once

#include "MurphyM4Batch.h"

// FreeInk SDK — board hardware profiles + build composition.
//
// A BoardProfile describes a device's pinout, screen, and capabilities. The
// runtime-active profile is BoardConfig::ACTIVE; drivers (display / input /
// power) read from it so the same code adapts to any board.
//
// A build is composed along two axes:
//   * DEVICES   (-DFREEINK_DEVICE_<NAME>) — which hardware the binary supports.
//   * CAPABILITIES (-DFREEINK_CAP_<NAME>) — which feature code is compiled in.
//
// Devices that share a binary must share an MCU and (to be runtime-selected)
// supply their own detection in the consumer. X3 and X4 are two profiles in one
// ESP32-C3 binary, picked at runtime via EInkDisplay::setDisplayX3() (which calls
// selectDevice); ACTIVE defaults to a compile-time default until then.

#include <Arduino.h>
#include <driver/gpio.h>  // gpio_hold_dis in releaseSdRail()
#include <esp_rom_sys.h>  // esp_rom_printf in holdPowerRails()

// ============================================================================
// Build composition — devices x capabilities
// ============================================================================

// --- 1) Devices are selected explicitly --------------------------------------
// RickyOS builds one device per binary: -DFREEINK_DEVICE_READPICO=1 or
// -DFREEINK_DEVICE_EEGO_A4=1 (both ESP32-S3). The Metalio E-Ink4 Plus port carries
// its own BoardConfig. Upstream FreeInk boards were removed on 2026-10-19.
#ifndef FREEINK_DEVICE_EEGO_A4
#define FREEINK_DEVICE_EEGO_A4 0
#endif
// Read Pico (小纸 Pico, RDP-G01-W) — ESP32-S3 N16R8 with an E0470A01 684x1216
// raw-parallel panel, SY7636A rails behind an FCA9555, CST836U touch,
// SC7A20H accelerometer and a CW32L010 PMU. See docs/engineering/read-pico.md.
#ifndef FREEINK_DEVICE_READPICO
#define FREEINK_DEVICE_READPICO 0
#endif

// --- 2) Coherence: exactly one device ----------------------------------------
#if (FREEINK_DEVICE_EEGO_A4 + FREEINK_DEVICE_READPICO) != 1
#error "FreeInk: select exactly one device: -DFREEINK_DEVICE_READPICO=1 or -DFREEINK_DEVICE_EEGO_A4=1."
#endif
#define FREEINK_MCU_S3 1

// --- 3) Derive panel drivers from the device set -----------------------------
// EEGO A4: UC8279C on SPI (Uc8279cA4Driver).
#if FREEINK_DEVICE_EEGO_A4
#define FREEINK_DRIVER_UC8279C_A4 1
#else
#define FREEINK_DRIVER_UC8279C_A4 0
#endif
// Read Pico (E0470A01): no on-glass controller, MCU-clocked rows. Its line timing
// has to come from the LCD_CAM hardware (XLE<-HSYNC, XCL<-PCLK, XSTL<-DE, CKV on
// RMT); an i80 bus has no DE signal, so the panel links EpdiyLcd (epdiy's LCD
// output path), not LovyanGFX's Bus_EPD (read-pico.md 2.4 item 3 / blocker B4).
#if FREEINK_DEVICE_READPICO
#define FREEINK_DRIVER_EPDIY_LCD 1
#else
#define FREEINK_DRIVER_EPDIY_LCD 0
#endif

// --- 4) Derive default capabilities (override with -DFREEINK_CAP_*=0/1) -------
#ifndef FREEINK_CAP_HAPTIC
#define FREEINK_CAP_HAPTIC 0
#endif

#ifndef FREEINK_CAP_TOUCH
#define FREEINK_CAP_TOUCH (FREEINK_DEVICE_EEGO_A4 || FREEINK_DEVICE_READPICO)
#endif
#ifndef FREEINK_CAP_FRONTLIGHT
#define FREEINK_CAP_FRONTLIGHT FREEINK_DEVICE_EEGO_A4
#endif
// Warm/cool color-temperature frontlight: a second warm PWM channel on top of
// the brightness one (FrontlightConfig::gpioWarm). Sub-capability of
// FREEINK_CAP_FRONTLIGHT — gates the warmth UI/settings out of single-channel
// builds (Paper Mono, de-link, Murphy, LilyGo). Within a multi-device build
// the profile's gpioWarm stays the runtime truth (hasColorTemperature()).
#ifndef FREEINK_CAP_WARMLIGHT
#define FREEINK_CAP_WARMLIGHT 0
#endif
// USB Mass Storage ("USB Transfer" mode): exposes the SD card to a host over
// USB-MSC. OPT-IN (default off), NOT board-derived, so a board enables it in
// its OWN env (e.g. -DFREEINK_CAP_USB_MSC=1). Native-USB (ESP32-S3/C3 OTG)
// targets only. When 0, UsbMassStorage links stub bodies and pulls in no
// TinyUSB/MSC code.
//
// ARDUINO_USB_MODE=0 is one way to reach the OTG PHY, not a requirement of this
// library. A firmware can equally keep ARDUINO_USB_MODE=1 — so USB Serial/JTAG
// stays the board's normal USB personality for monitoring and flashing — and
// switch the shared PHY to OTG at runtime for the duration of a transfer,
// handing it back before it reboots out of the mode. X4 Pro and LilyGo T5 S3
// both ship that way in CrossPoint.
//
// What this DOES require is the platform's prebuilt Arduino core, whose TinyUSB
// component is built with CONFIG_TINYUSB_MSC_ENABLED: an env that rebuilds the
// core from source (custom_sdkconfig / custom_component_remove) drops that
// component and USBMSC will not link.
//
// Storage must expose a 512-byte-sector block device. Both SDCardManager
// backends do — SDMMC natively, and SPI through SdFat's own SdCard — so this is
// no longer an SDMMC-only capability.
#ifndef FREEINK_CAP_USB_MSC
#define FREEINK_CAP_USB_MSC 0
#endif
// BLE HID host. The BleKeyboardHost lib pairs/connects to Bluetooth Low Energy
// HID peripherals such as keyboards and page turners and emits translated key
// events; it compiles its NimBLE central code only when this is set, otherwise
// it links stub bodies and pulls in no BLE code at all. Default off: it's an
// opt-in feature, not board-derived. ESP32-C3/S3 targets only (BLE required).
#ifndef FREEINK_CAP_BLE_HID_HOST
#ifdef FREEINK_CAP_BLE_KEYBOARD
#define FREEINK_CAP_BLE_HID_HOST FREEINK_CAP_BLE_KEYBOARD
#else
#define FREEINK_CAP_BLE_HID_HOST 0
#endif
#endif
#ifndef FREEINK_CAP_BLE_KEYBOARD
#define FREEINK_CAP_BLE_KEYBOARD FREEINK_CAP_BLE_HID_HOST
#endif
// Scan-list policy for the BLE HID host. Default hides anonymous non-HID
// advertisers so firmware pairing UIs are not filled with random beacon
// addresses. Set -DFREEINK_BLE_HID_SHOW_UNNAMED_DEVICES=1 during bring-up to
// include connectable unnamed devices as probe candidates. Devices advertising
// HID are always kept, even without a name.
#ifndef FREEINK_BLE_HID_SHOW_UNNAMED_DEVICES
#define FREEINK_BLE_HID_SHOW_UNNAMED_DEVICES 0
#endif
// Security policy for BLE HID host pairing. Default to Just Works bonding
// because many page-turner remotes have no input/display capability and reject
// mandatory MITM/passkey pairing. Firmware that specifically wants keyboard
// passkey pairing can opt in with -DFREEINK_BLE_HID_REQUIRE_MITM=1.
#ifndef FREEINK_BLE_HID_REQUIRE_MITM
#define FREEINK_BLE_HID_REQUIRE_MITM 0
#endif

// I2C fuel-gauge battery backend. Compiled in when a build contains a gauge
// device (X3's BQ27220, or LilyGo's BQ27220+BQ25896). Selection is then *runtime*
// per active profile (BatteryMonitor uses the gauge only when
// ACTIVE.batteryGauge.gaugeAddr != 0) — required because X3 (gauge) and X4 (ADC)
// share one C3 binary.
#ifndef FREEINK_BATTERY_I2C_GAUGE
#define FREEINK_BATTERY_I2C_GAUGE FREEINK_DEVICE_READPICO
#endif
#ifndef FREEINK_CAP_COLOR
#define FREEINK_CAP_COLOR 0
#endif
#ifndef FREEINK_CAP_AUDIO
#define FREEINK_CAP_AUDIO 0
#endif
// Microphone capture (PDM in). Separate from FREEINK_CAP_AUDIO (output): the
// Sticky has a PDM mic but no output codec. The Microphone lib compiles its
// i2s_pdm RX path only when this is set; otherwise it links stub bodies.
#ifndef FREEINK_CAP_MIC
#define FREEINK_CAP_MIC 0
#endif
// On-board I2C sensors. Each lib (Rtc / EnvironmentSensor / Imu) compiles its
// I2C driver only when its flag is set; otherwise it links stub bodies.
#ifndef FREEINK_CAP_RTC
#define FREEINK_CAP_RTC (FREEINK_DEVICE_EEGO_A4 || FREEINK_DEVICE_READPICO)
#endif
#ifndef FREEINK_CAP_TEMP_HUMIDITY
#define FREEINK_CAP_TEMP_HUMIDITY 0
#endif
#ifndef FREEINK_CAP_IMU
#define FREEINK_CAP_IMU FREEINK_DEVICE_READPICO
#endif
// LEDC PWM buzzer (tone beeper). The Buzzer lib drives the AudioConfig.buzzer
// pin; on for boards that wire one (Sticky GPIO48, Murphy GPIO46, PaperS3
// GPIO21, Read Pico GPIO2). Separate from FREEINK_CAP_AUDIO — a buzzer is a tone
// device, not a WAV/codec output.
#ifndef FREEINK_CAP_BUZZER
#define FREEINK_CAP_BUZZER FREEINK_DEVICE_READPICO
#endif
#ifndef FREEINK_CAP_LED
#define FREEINK_CAP_LED 0
#endif
#ifndef FREEINK_CAP_NET_TLS13
#if defined(FREEINK_NET_WOLFSSL)
#define FREEINK_CAP_NET_TLS13 1
#else
#define FREEINK_CAP_NET_TLS13 0
#endif
#endif

// Place the facade framebuffer(s) in PSRAM (heap, MALLOC_CAP_SPIRAM) instead of
// static DRAM .bss. Default on for M5Paper v1.1: the classic ESP32 has tight
// internal DRAM but 8MB PSRAM, and the 63KB 540x960 framebuffer does not fit in
// .bss alongside the firmware. Every other device keeps the static DRAM array.
// (The prebuilt Arduino-ESP32 libs disable BSS-in-PSRAM, so this is a runtime
// heap allocation, not EXT_RAM_BSS_ATTR.)
// Read Pico also opts in: its 1-bpp 1216/8 x 684 = 103,968-byte framebuffer is
// larger than every existing panel's and has no business in the S3's internal
// DRAM next to the ~56.5 KiB of internal-DMA LUT tables the Lgfx path already
// needs (docs/engineering/read-pico.md 2.6).
#ifndef FREEINK_FB_PSRAM
#define FREEINK_FB_PSRAM FREEINK_DEVICE_READPICO
#endif

// SD transport. de-link (4-bit) and X4 Pro (1-bit) are wired for SDMMC; SdFat
// can't drive SDIO, so they get a native esp-idf SDMMC block device behind
// SDCardManager. Every other board stays on SdFat-over-SPI. The consumer's build
// must define USE_BLOCK_DEVICE_INTERFACE=1 for the SdFat FsVolume these mount on.
// Read Pico is 1-bit SDMMC too (CLK38 CMD42 D0=44), with card detect on an
// FCA9555 input rather than a GPIO.
// Override with -DFREEINK_SD_SDMMC=0/1.
#ifndef FREEINK_SD_SDMMC
#define FREEINK_SD_SDMMC FREEINK_DEVICE_READPICO
#endif

// Serial log transport hint for consumer firmware. Boards can share the same MCU
// but expose logs differently: LilyGo T5 S3 is monitored over native USB CDC,
// while Sticky bring-up is more reliable through the IDF/ROM console path.
#define FREEINK_LOG_TRANSPORT_SERIAL 0
#define FREEINK_LOG_TRANSPORT_USB_CDC_WRITE 1
#define FREEINK_LOG_TRANSPORT_ROM_PRINTF 2
#ifndef FREEINK_LOG_TRANSPORT
#define FREEINK_LOG_TRANSPORT FREEINK_LOG_TRANSPORT_SERIAL
#endif

// Bidirectional serial transport exposed by the board's physical USB-C port.
// Most boards route it through Arduino's selected Serial implementation, while
// Sticky's on-board WCH bridge is wired to UART0 instead of native USB CDC.
#define FREEINK_SERIAL_HAS_TX_TIMEOUT (ARDUINO_USB_CDC_ON_BOOT)

namespace BoardConfig {

inline auto& serialTransport() { return Serial; }

// Physical device family. X3 and X4 are sibling devices on the same ESP32-C3
// board (identical pinout, different panel/size): both profiles compile into the
// C3 binary and one is chosen at runtime (setDisplayX3() -> selectDevice).
enum class Board : uint8_t {
  XteinkX4,
  XteinkX3,
  XteinkX3Uc8279,   // newer X3 production run: same board/glass, UC8279d controller
  XteinkX4Pro,      // ESP32-S3 sibling of the C3 X4: SSD1677 + GT911 touch + warm/cold frontlight
  XteinkX4Classic,  // ESP32-S3 X4 Classic: X4 Pro display stack without touch/frontlight
  M5StackPaperColor,
  MurphyM3,
  MurphyM4,
  DeLink,
  LilyGoT5S3,
  M5PaperV11,
  Sticky,
  PaperMono,
  M5PaperS3,  // ESP32-S3 sibling of M5Paper v1.1: same ED047TC1 glass, no IT8951 — raw parallel via LovyanGFX
  EegoA4,     // EEGO Reader A4: ESP32-S3, UC8279C 768x552 SPI panel, GSLX680 touch, PCF8563 RTC
  WaveshareEpaper397,
  OnePage,  // OnePage: ESP32-C61, SSD1677 800x480 SPI panel, 4-key ADC ladder + 3 side keys
  MetalioEink4,
  ReadPico,  // Read Pico (RDP-G01-W): ESP32-S3 N16R8, E0470A01 684x1216 raw-parallel EPD
             // (16-bit i80 bus, SY7636A rails behind an FCA9555), CST836U touch +
             // 3 capacitive key zones, SC7A20H accelerometer, CW32L010 PMU
  WsEpaper397,  // SDK-managed Waveshare power and ordinary buttons.
  MetalioEInk4,  // Upstream identity; local MetalioEink4 retains its calibration.
};

// How the board reports button presses.
enum class InputStyle : uint8_t {
  XteinkAdcLadder,          // resistor ladder on two ADC pins (X3/X4)
  DigitalButtons,           // plain active-low GPIO buttons
  DigitalConfirmBackHold,   // confirm held > N ms synthesizes BACK (M5 PaperColor)
  DigitalConfirmPowerHold,  // confirm click, power hold on a shared GPIO
  DigitalFiveKey,           // 3 physical GPIO keys + synthesized events (Murphy M3)
  DigitalTwoButton,         // short up/down; holds synthesize back/confirm/power
  DigitalFunctionMultiGesture,  // function click/double/hold plus direction-key hold gestures
  OnePageAdcLadder,         // OnePage: 4 front keys on GPIO4 ADC ladder + 3 side GPIO keys
  // Read Pico: NO GPIO buttons at all. Navigation comes from the CST836U touch
  // plus three capacitive key zones that live in the strip of the (taller) touch
  // panel BELOW the drawn image — they are hit-tested on raw coordinates by the
  // board's InputManager::ButtonHook, not by any input style branch — and the
  // power key is owned by the CW32L010 PMU (events over I2C, never a GPIO).
  // DigitalButtons would be vacuous-but-misleading. Consumers compare against
  // specific InputStyle values, so appending changes no existing branch.
  ReadPicoTouchStrip,
};

// Panel controller silicon. Drivers are selected from this at begin().
// LgfxEpd = a raw-parallel EPD with no on-glass controller, driven via LovyanGFX
// (e.g. ED047TC1 on LilyGo T5 S3).
// UC8179 and UC8279 are the UltraChip siblings that newer batches ship in place
// of the original controller (UC8179/UC8279 for the X4 family's SSD1677, UC8279d
// for the X3's UC8253). Same UC81xx KW command family, separate drivers. Which
// one a unit carries is resolved at boot by the display-bus probe (0x70 VER /
// 0x71 FLG read, which SSD1677 lacks; VER byte2 LUT_VER tells UC8179 from
// UC8279). NVS hw_calib/screenType is read for diagnostics only. See
// XteinkDetect::applyXteinkDisplayController.
enum class DisplayController : uint8_t {
  SSD1677 = 0,
  UC8253 = 2,
  ED2208 = 3,
  LgfxEpd = 4,
  IT8951 = 5,
  UC8279 = 6,
  UC8179 = 7,
  UC8279C = 8,
};

// Optional capacitive touch controller.
// Cst836u (Read Pico) needs its own backend rather than reusing Cst816s: the
// CST816S driver's register/frame protocol does not match. CST836U reads its point
// frame from register 0x00 (CST816S uses 0x02), each point is 6 bytes, up to two
// simultaneous points are reported, and its active-low reset is an FCA9555 Port-0
// line instead of a GPIO. The 7-bit address happens to coincide (both 0x15), which
// is exactly why the controller enum — not the address — has to disambiguate.
// Appended last so every existing enumerator keeps its value.
enum class TouchController : uint8_t { None, Chsc6x, Gt911, Ft5x06, Gslx680, Ft6336u, Cst816s, Cst836u };

// Optional audio output path. Murphy M3 ships an ES8388-compatible stereo
// codec (I2S slave, control over the shared touch I2C bus) — the contract was
// recovered from the OEM firmware dump; see the consumer's audio notes.
// M5 PaperColor ships an ES8311 mono codec + AW8737A speaker amp — the
// contract comes from the official pin map and M5Unified's speaker bring-up.
enum class AudioOutput : uint8_t { None, I2sDac, I2sEs8388, I2sEs8311, I2sEs8311Mclk16k, PwmBuzzer, MetalioModule };

// Optional addressable RGB LED strip. PaperColor has two RGB LEDs on GPIO21
// behind the M5PM1 LDO3V3 RGB rail.
enum class LedColorOrder : uint8_t { RGB, GRB };

constexpr int8_t PIN_UNASSIGNED = -1;

struct DisplayPins {
  int8_t sclk;
  int8_t mosi;
  int8_t cs;
  int8_t dc;
  int8_t rst;
  int8_t busy;
  int8_t powerEnable;
};

struct SdPins {
  int8_t sclk;
  int8_t miso;
  int8_t mosi;
  int8_t cs;
  int8_t powerEnable;
  bool separateSpi;
  uint32_t spiHz;  // 0 = use the SD manager default (40 MHz)
  // Polarity of powerEnable. true (default) = active-high (drive HIGH to power the
  // card, LOW to cut it) as on most boards. false = active-LOW enable (e.g. X4 Pro's
  // GPIO5, which gates the card while held LOW); the sleep path must then drive it
  // HIGH to power the card down. Defaulted so existing initializers stay valid.
  bool powerActiveHigh = true;
};

// 4-bit SDMMC/SDIO wiring (e.g. de-link). SdFat can't drive SDIO, so a board with
// busWidth != 0 gets the native esp-idf SDMMC block device instead of SPI/SdFat.
struct SdmmcPins {
  int8_t clk;
  int8_t cmd;
  int8_t d0;
  int8_t d1;
  int8_t d2;
  int8_t d3;
  uint8_t busWidth;  // 0 = not an SDMMC board (use SdPins/SPI), 1 or 4 = SDMMC
};

// The I2C fuel-gauge silicon a board carries. Each type has its own register map and
// init, so BatteryMonitor dispatches on it. Bq27220: TI command registers, no profile
// upload (LilyGo/X3). Cw2017: CellWise gauge that needs an 80-byte BATINFO battery
// profile loaded before it reports a valid SoC (Xteink X4 Pro).
// Axp2101: not a gauge chip but a PMIC whose fuel-gauge block reports SoC directly
// (Waveshare ESP32-S3-ePaper-3.97). Its registers live in Axp2101.h.
// profile loaded before it reports a valid SoC (Xteink X4 Pro). Cw32L010Pmu (Read
// Pico) is not a register map at all: battery mV / SoC / charge state arrive over the
// CW32L010 PMU's length-prefixed CRC frame protocol, or from its 8-byte QUICK_BATTERY
// register. Appended last so every existing enumerator keeps its value.
enum class GaugeType : uint8_t { Bq27220, Cw2017, Axp2101, Cw32L010Pmu };

// I2C fuel-gauge / charger wiring (e.g. BQ27220 + BQ25896 on LilyGo T5 S3). When
// gaugeAddr != 0 (and FREEINK_BATTERY_I2C_GAUGE is set), BatteryMonitor reads the
// gauge over I2C instead of an ADC pin. chargerAddr is optional (0 = none) and
// only used for charge status.
struct BatteryGaugeConfig {
  int8_t i2cSda;
  int8_t i2cScl;
  uint32_t i2cHz;
  uint8_t gaugeAddr;    // BQ27220 = 0x55; CW2017 = 0x63; 0 = no I2C gauge (use ADC)
  uint8_t chargerAddr;  // BQ25896 = 0x6B; 0 = none
  // Arduino I2C controller index: 0 = Wire, 1 = Wire1. Default 0. Set to 1 on
  // boards where the gauge sits on a different physical bus than another I2C
  // peripheral (e.g. Sticky's GT911 touch on Wire/SDA3-SCL2 vs gauge on
  // Wire1/SDA1-SCL0) so they don't fight over one controller. Only honored on
  // multi-bus SoCs (SOC_I2C_NUM > 1); single-bus parts (ESP32-C3) ignore it.
  uint8_t i2cBus = 0;
  GaugeType gaugeType = GaugeType::Bq27220;  // register map / init to use
};

struct InputPins {
  int8_t back;
  int8_t confirm;
  int8_t left;
  int8_t right;
  int8_t up;
  int8_t down;
  int8_t power;
  bool powerActiveHigh;  // true = pressed reads HIGH (INPUT_PULLDOWN); false = active-LOW (INPUT_PULLUP)
  int8_t adcLadderPin = PIN_UNASSIGNED;  // ADC pin for single resistor ladder (e.g. OnePage GPIO4)
};

// Capacitive touch panel description (TouchController::None disables it).
struct TouchConfig {
  TouchController controller;
  int8_t sda;
  int8_t scl;
  int8_t irq;
  int8_t reset;
  uint8_t i2cAddress;
  uint16_t rawMinX, rawMaxX;  // raw controller range, mapped to display coords
  uint16_t rawMinY, rawMaxY;
  bool synthesizeConfirm;  // emit a CONFIRM button event on tap
  uint8_t i2cAddressAlt;   // alternate I2C address to probe (GT911 0x14; 0 = none)
  bool irqActiveLow;       // touch IRQ asserted LOW (CHSC6x)
  // GT911 point-frame layout: false = datasheet standard (track-id at 0x8150, so
  // coords start at byte 1); true = coords start at byte 0 (no track-id), as seen
  // on M5Paper's GT911 which boots without a reset/config dance. Ignored (CHSC6x).
  bool gt911CoordsAtByte0;
  // Touch power-rail enable. PIN_UNASSIGNED on boards whose touch controller is always
  // powered; otherwise driven to its ON level before the reset/probe on boards that
  // gate it (e.g. Sticky's active-high TOUCH_EN, or the X4 Pro's active-low GPIO2).
  // Default keeps existing initializers valid.
  int8_t powerEnable = PIN_UNASSIGNED;
  // Touch-to-panel mounting correction, applied to the raw coords so the touch
  // frame aligns with the display's NATIVE (panel) frame before orientation
  // mapping. swapXY first (digitizer rotated 90° vs panel, e.g. Sticky's portrait
  // sensor on a landscape panel), then per-axis flip. rawMinX/MaxX/etc describe the
  // POST-swap (panel) axes. Defaults = aligned. The display orientation is handled
  // separately by GfxRenderer::tapToLogical, so taps follow rotation automatically.
  bool swapXY = false;
  bool flipX = false;
  bool flipY = false;
  // Capacitive home key below the panel, reported by the touch controller itself
  // (GT911 "have key" status bit 0x10, surfaced as InputManager::wasHomeKeyPressed()).
  // Lets firmware move "exit to home" off a swipe gesture on boards that have one.
  bool hasHomeKey = false;
  // Polarity of powerEnable. true (default) = active-high (drive HIGH to power the
  // controller). false = active-LOW (drive LOW to power it, e.g. X4 Pro's GPIO2). The
  // reset path drives the ON level; the sleep path drives the OFF level.
  bool powerEnableActiveHigh = true;
};

// PWM frontlight description (gpio == PIN_UNASSIGNED disables it).
struct FrontlightConfig {
  int8_t gpio;  // primary channel: the sole LED on a single-channel board, or the "cool"
                // channel of a warm/cool pair.
  uint32_t pwmFrequency;
  uint8_t pwmResolutionBits;
  bool activeHigh;
  // Optional second PWM channel for a warm/cool color-temperature frontlight (e.g. the
  // Xteink X4 Pro: cool=gpio GPIO8, warm=gpioWarm GPIO9). PIN_UNASSIGNED on single-channel
  // boards (de-link / LilyGo / Murphy), where setColorTemperature() stays a no-op. The warm
  // channel shares the primary's frequency / resolution / active level. FrontlightManager
  // treats `gpio` as cool and `gpioWarm` as warm; if a board's pair is physically reversed,
  // the color-temperature direction inverts (cosmetic, and user-flippable in firmware).
  int8_t gpioWarm = PIN_UNASSIGNED;
  // Paper Mono: the frontlight PWM is generated by the M5PM1 PMIC (its GPIO3
  // routed to alt-function PWM0 drives an AW9967 boost LED driver), not by an
  // ESP LEDC channel. `gpio` stays PIN_UNASSIGNED (ESP GPIO3 is a button
  // there!); FrontlightManager talks to the PMIC over I2C instead. The AW9967
  // is fed from the EPD rail, so the frontlight only lights while EPD power is
  // on. pwmFrequency still applies (PM1 PWM_FREQ register); resolution is the
  // PM1's fixed 12 bits.
  bool viaPm1Pwm = false;
};

// Audio output description (AudioOutput::None disables it).
struct AudioConfig {
  AudioOutput output;
  int8_t bclk;    // I2S bit clock (unused for PWM buzzer)
  int8_t lrclk;   // I2S word select (unused for PWM buzzer)
  int8_t dout;    // I2S data out, or the PWM pin for a buzzer
  int8_t mclk;    // I2S master clock (PIN_UNASSIGNED if not wired)
  int8_t enable;  // codec power / rail enable pin (PIN_UNASSIGNED if none)
  bool enableActiveHigh;
  int8_t ampEnable;  // separate speaker-amp enable (e.g. AW8737A SPK_EN), held
                     // high only while playing; PIN_UNASSIGNED if none. Active-high.
  int8_t codecSda;   // codec control I2C — may be a shared bus (e.g. touch)
  int8_t codecScl;
  uint8_t codecAddr;  // 7-bit codec address, 0 = no control codec
  int8_t buzzer;      // separate LEDC tone pin (PIN_UNASSIGNED if none)
  uint16_t ampSettleMs = 0;  // silence after amp enable before PCM; board-calibrated
};

struct LedConfig {
  int8_t data;
  uint8_t count;
  LedColorOrder colorOrder;
  bool pmicRgbPower;  // true = enable M5PM1 RGB LED power rail before use
  // Paper Mono: one discrete RGB LED with no addressable controller — red is
  // the M5PM1's LED output (PWR_CFG bit 4), green/blue are M5IOE1 pins IO8/IO9.
  // On/off per channel (no per-channel intensity); `data` stays PIN_UNASSIGNED.
  bool paperMonoDiscrete = false;
};

// Microphone input path (MicInput::None disables it). PDM mics (e.g. the Sticky's
// MSM261DDB020) need a clock out + data in; `enable` powers the mic rail.
enum class MicInput : uint8_t { None, Pdm };
struct MicConfig {
  MicInput input;
  int8_t clk;     // PDM clock (output to mic)
  int8_t data;    // PDM data (input from mic)
  int8_t enable;  // mic power/enable pin (PIN_UNASSIGNED if none)
  bool enableActiveHigh;
};

// RtcType::Cw32L010Pmu (Read Pico) is the CW32L010 PMU's own RTC: unix seconds and
// an alarm mode/target reached through the PMU frame protocol (PMU_CMD_TIME_GET /
// TIME_SYNC / ALARM_SET), not a register map, so it cannot map onto an existing
// PCF/DS/RX type. ImuType::Sc7a20h is a separate backend: address 0x19, WHO_AM_I
// 0x11, its own register map and accelerometer-only (no gyroscope). Both are
// appended last so every existing enumerator keeps its value.
enum class RtcType : uint8_t { None, Pcf8563, Pcf85063, Ds3231, Rx8130, Rx8010, Cw32L010Pmu };
enum class ImuType : uint8_t { None, Lsm6ds3, Qmi8658, Sc7a20h };

// On-board I2C sensors sharing one bus (e.g. the Sticky's RTC + temp/humidity +
// IMU on SDA1/SCL0, the same bus as its fuel gauge). Each addr is 0 when that
// sensor is absent; the matching sensor lib reads its addr from here.
struct SensorsConfig {
  int8_t i2cSda;
  int8_t i2cScl;
  uint32_t i2cHz;
  uint8_t rtcAddr;           // PCF8563 = 0x51, DS3231 = 0x68; 0 = none
  uint8_t tempHumidityAddr;  // SHT40 = 0x44; 0 = none
  uint8_t imuAddr;           // LSM6DS3TR-C = 0x6A, QMI8658 = 0x6B/0x6A; 0 = none
  uint8_t i2cBus = 0;        // 0 = Wire, 1 = Wire1 on multi-bus SoCs
  RtcType rtcType = RtcType::None;
  ImuType imuType = ImuType::None;
  // IMU mount correction, applied to raw accel + gyro samples so Imu::read()
  // reports a board-independent frame: gyro X = the portrait left/right tilt
  // axis, gyro Y = the landscape one (the X3/X4 convention existing consumers
  // assume). swapXY first (IMU rotated 90° on the PCB), then per-axis sign
  // flip — same ordering as TouchConfig's swapXY/flip mapping.
  bool imuSwapXY = false;
  bool imuFlipX = false;
  bool imuFlipY = false;
};

// How the panel is mounted relative to the driver's native scan. Any board injects
// its own mirroring here; a 180° rotation is mirrorX && mirrorY. (90°/270° need a
// software transpose — they swap width/height and aren't expressible by panel RAM
// addressing alone — so they are not a flag here.)
struct DisplayOrientation {
  bool mirrorX;  // reverse source/column (X) order
  bool mirrorY;  // reverse gate/row (Y) order
};

// I2C frontlight controller (LM3630A on the EEGO A4). The controller is driven
// over a board I2C bus with a separate enable GPIO; the enable is also the
// hardware probe: an unpopulated optional circuit (some retail A4 units ship
// without a frontlight) never ACKs, so FrontlightManager only reports present()
// after a successful probe.
enum class I2cFrontlightController : uint8_t { None, Lm3630a };
struct I2cFrontlightConfig {
  I2cFrontlightController controller;
  int8_t sda;
  int8_t scl;
  uint32_t i2cHz;
  uint8_t address;
  int8_t enable;
};
constexpr I2cFrontlightConfig NO_I2C_FRONTLIGHT = {I2cFrontlightController::None, PIN_UNASSIGNED, PIN_UNASSIGNED, 0,
                                                   0, PIN_UNASSIGNED};

// Power-rail latch pins a battery-powered board must drive HIGH early in boot
// to keep itself on (PWR_HOLD / PWR_LOCK style latches, e.g. the Sticky's
// GPIO45/46). Board truth lives here; asserting them is firmware policy — see
// holdPowerRails(). Releasing the pins later is a software power-off.
struct PowerConfig {
  int8_t latch0 = PIN_UNASSIGNED;
  int8_t latch1 = PIN_UNASSIGNED;
  // Battery-charger enable input (e.g. the Sticky's EN_BAT_CHGn -> BQ25616 /CE
  // on GPIO39). holdPowerRails() drives it to its active level and latches it
  // with gpio_hold_en so the charger stays enabled awake AND through deep sleep.
  // Left unmapped, an S3 JTAG-group pin like GPIO39 keeps its reset-default weak
  // pull-up while the firmware runs — /CE sits high and the device won't charge
  // until sleep isolates the pad and lets the line float back to enabled.
  int8_t chargeEnable = PIN_UNASSIGNED;
  bool chargeEnableActiveHigh = false;  // "n"-suffixed enables are active-low
};

// Panel rows/columns the device's bezel physically overlaps, in the panel's
// native portrait frame; firmware keeps content out of them. The default is
// the value CrossPoint historically hardcoded for every board (tuned on the
// X4 bezel) — override per profile as boards are measured.
struct ViewableInsets {
  uint8_t top = 9;
  uint8_t right = 3;
  uint8_t bottom = 3;
  uint8_t left = 3;
};

struct HapticConfig {
  int8_t gpio = PIN_UNASSIGNED;
  bool activeHigh = true;
  uint32_t pwmFrequency = 20000;
};

struct BoardProfile {
  Board board;
  const char* name;
  InputStyle inputStyle;
  DisplayController displayController;
  uint16_t displayWidth;
  uint16_t displayHeight;
  DisplayPins display;
  uint32_t displaySpiHz;  // 0 = use the panel driver's controller-appropriate default
  SdPins sd;
  InputPins input;
  int8_t batteryAdc;
  int8_t batteryChargeStatus;
  float batteryDividerMultiplier;
  int8_t usbDetect;
  TouchConfig touch;
  FrontlightConfig frontlight;
  AudioConfig audio;
  LedConfig leds;
  DisplayOrientation orientation;   // panel mount transform (mirrorX/mirrorY)
  SdmmcPins sdmmc;                  // 4-bit SDMMC wiring (busWidth 0 = use SPI/SdFat)
  BatteryGaugeConfig batteryGauge;  // I2C fuel gauge (gaugeAddr 0 = use ADC pin)
  // Microphone (PDM in). Defaulted so existing profiles need no change; a board
  // with a mic sets it. PIN_UNASSIGNED is -1 — do NOT rely on zero-init here.
  MicConfig mic = {MicInput::None, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, true};
  // On-board I2C sensors (RTC / temp+humidity / IMU). Defaulted to "none"; a
  // board with sensors sets the bus pins + each present sensor's address.
  SensorsConfig sensors = {PIN_UNASSIGNED, PIN_UNASSIGNED, 0, 0, 0, 0, 0, RtcType::None, ImuType::None};
  // UI scale multiplier the firmware applies to its theme metrics and chrome fonts.
  // 1.0 keeps the original button-era pixel sizes. Touch devices bump this so rows,
  // buttons, and tap targets are finger-sized: these panels are ~220-235 PPI, so a
  // 30px row is only ~3mm. Per-board and hand-tuned (PPI alone can't tell the 4.26"
  // X4 from the 3.97" Sticky); the firmware owns how it maps to metrics/fonts.
  float uiScale = 1.0f;
  // Power-rail latch pins (see PowerConfig). Defaulted so existing profiles
  // need no change; a board with a latch sets it.
  PowerConfig power = {};
  // Panel-controller variant byte, filled in at boot by the display probe when it
  // matters (UC8279 800x480: VER byte2 LUT_VER, 0x02 vs 0x68 — selects which AA
  // waveform table the driver uploads). 0 = not probed / not applicable.
  uint8_t displayControllerVariant = 0;
  // Bezel-covered edge insets. Defaulted so existing profiles need no change;
  // a measured board overrides it.
  ViewableInsets viewableInsets = {};
  // Polarity of batteryChargeStatus. Default is the MCP73832-style /STAT that
  // every earlier board uses: open-drain, LOW = charging, read with the internal
  // pull-up. true = the line is push-pull driven HIGH while charging and carries
  // no pull (the X4 Pro's GPIO21, recovered from the stock Cw2017PowerHal —
  // stock configures it input/no-pull and reports the raw level).
  bool batteryChargeStatusActiveHigh = false;
  // I2C frontlight (LM3630A). Defaulted so existing profiles need no change;
  // a board with one sets it (EEGO A4).
  I2cFrontlightConfig i2cFrontlight = NO_I2C_FRONTLIGHT;
  // Native image grayscale capability; omitted by legacy aggregate profiles.
  uint8_t grayscaleLevels = 4;
  HapticConfig haptic = {};
};

constexpr FrontlightConfig NO_FRONTLIGHT = {PIN_UNASSIGNED, 0, 0, true};
constexpr AudioConfig NO_AUDIO = {AudioOutput::None,
                                  PIN_UNASSIGNED,
                                  PIN_UNASSIGNED,
                                  PIN_UNASSIGNED,
                                  PIN_UNASSIGNED,
                                  PIN_UNASSIGNED,
                                  true,
                                  PIN_UNASSIGNED,
                                  PIN_UNASSIGNED,
                                  PIN_UNASSIGNED,
                                  0,
                                  PIN_UNASSIGNED};
constexpr LedConfig NO_LEDS = {PIN_UNASSIGNED, 0, LedColorOrder::GRB, false};

// Defaults matching the BoardProfile member initializers, so a profile can set a
// trailing field (e.g. uiScale) positionally without spelling out the literals.
constexpr MicConfig NO_MIC = {MicInput::None, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, true};

constexpr DisplayOrientation NO_FLIP = {false, false};   // native scan
constexpr SdmmcPins NO_SDMMC = {
    PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 0};
constexpr BatteryGaugeConfig NO_GAUGE = {PIN_UNASSIGNED, PIN_UNASSIGNED, 0, 0, 0};  // ADC battery

// --- Xteink X4 — ESP32-C3, SSD1677 (800x480) ---------------------------------
// Default 10 MHz, like the other Xteink profiles. Preserve the explicit legacy
// 40 MHz opt-in for consumers that have validated it on their hardware.
#ifdef FREEINK_X4_OVERCLOCK_SPI
#define FREEINK_X4_DISPLAY_SPI_HZ 40000000u
#else
#define FREEINK_X4_DISPLAY_SPI_HZ 10000000u
#endif

// --- M5Stack Paper Mono / PaperS3 — ESP32-S3, 800x480 SSD1677 --------------
// EPD power/reset and the microSD rail are switched by the on-board M5IOE1;
// their direct GPIO fields stay unassigned and the consumer board-support
// library supplies the corresponding hooks (see M5Ioe1.h for the pin map).

// --- EEGO A4 — ESP32-S3 N16R8, UC8279C + GSLX680 ---------------------------
// This profile follows the validated EEGO A4 template pinout and calibration.
constexpr BoardProfile EEGO_A4 = {Board::EegoA4,
                                  "eego_a4",
                                  InputStyle::DigitalButtons,
                                  DisplayController::UC8279C,
                                  768,
                                  552,
                                  {42, 45, 21, 14, 13, 41, 6},
                                  20000000,
                                  {39, 40, 38, 47, PIN_UNASSIGNED, true, 20000000},
                                  {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 5, 7, 8, true},
                                  10,
                                  11,
                                  1.559f,
                                  PIN_UNASSIGNED,
                                  // The GSLX680 backend applies the official 1.2.7 calibration
                                  // (InputManager::pollGslx680) and returns panel-native
                                  // x=0..767, y=0..551; no raw-range mapping is needed here.
                                  {TouchController::Gslx680, 2, 1, PIN_UNASSIGNED, 3, 0x40, 0, 767, 0, 551, false, 0,
                                   false, false, PIN_UNASSIGNED, false, false, false, true, true},
                                  NO_FRONTLIGHT,
                                  NO_AUDIO,
                                  NO_LEDS,
                                  NO_FLIP,
                                  NO_SDMMC,
                                  NO_GAUGE,
                                  NO_MIC,
                                  {2, 1, 400000, 0x51, 0, 0, 0, RtcType::Pcf8563, ImuType::None},
                                  1.2f,
                                  {4},
                                  0,  // displayControllerVariant (not probed for UC8279C)
                                  {},  // viewableInsets
                                  false,  // batteryChargeStatusActiveHigh
                                  // LM3630A on the shared I2C bus (SDA2/SCL1), enable GPIO12.
                                  // Probed at runtime: some retail A4 units have no frontlight.
                                  {I2cFrontlightController::Lm3630a, 2, 1, 400000, 0x36, 12}};

static_assert(!EEGO_A4.touch.swapXY && !EEGO_A4.touch.flipX && !EEGO_A4.touch.flipY,
              "EEGO A4 touch backend returns panel-native coordinates; no raw-range mapping");

// --- Read Pico (RDP-G01-W) — ESP32-S3 N16R8, E0470A01 684x1216 raw-parallel EPD
// (16-bit bus, SY7636A rails behind an FCA9555), CST836U touch + 3 capacitive
// key zones, SC7A20H accelerometer, CW32L010 PMU, 1-bit SDMMC, GPIO2 buzzer.
// Sources: MindReset/read_pico_firmware README + components/{read_pico,
// read_pico_pmu,cst836u,sc7a20h,fca9555,sy7636a,e0470_epaper_waveform,epdiy},
// main/{apps,ui,sleep.c}. See docs/engineering/read-pico.md.
//
// Geometry: displayWidth/displayHeight are the native SCAN frame (source columns
// x gate lines), exactly like LilyGo T5 S3 — epdiy's E0470_DISPLAY is
// {.width = 1216, .height = 684} and LgfxEpdDriver::setup() assigns them straight
// to Panel_EPD's panel_width/panel_height. The app-facing portrait glass is
// 684 x 1216 after the driver's rotation; see read-pico.md 1.2.
//
// Board-support that cannot live in this profile (no GPIO of its own): the CST836U
// reset, the EPD rails and the SD card detect are all FCA9555 Port-0 lines, the
// battery/time/power handoff is the PMU frame protocol, and the panel's 16-bit i80
// wiring + power hooks are the board's LgfxEpdConfig. Those live in BoardReadPico.
constexpr AudioConfig READ_PICO_AUDIO = {AudioOutput::None,    PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
                                         PIN_UNASSIGNED,       PIN_UNASSIGNED, true,           PIN_UNASSIGNED,
                                         PIN_UNASSIGNED,       PIN_UNASSIGNED, 0,              2};

constexpr BoardProfile READ_PICO = {
    Board::ReadPico,
    "read_pico",
    InputStyle::ReadPicoTouchStrip,
    DisplayController::LgfxEpd,
    1216,  // displayWidth: source columns (epdiy E0470_DISPLAY.width)
    684,   // displayHeight: gate lines  (epdiy E0470_DISPLAY.height)
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
     PIN_UNASSIGNED},  // no SPI display pins: the 16-bit i80 bus lives in LgfxEpdConfig
    0,                // displaySpiHz n/a (external bus)
    // SPI view of the SD slot is unused; SD is 1-bit SDMMC (see sdmmc below) and
    // the card has no power gate (powerEnable unassigned).
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, false, 0},
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
     PIN_UNASSIGNED, false},  // no GPIO buttons: keys are the touch strip + the PMU power key
    PIN_UNASSIGNED,  // batteryAdc: none — the CW32L010 PMU reports battery/SoC
    PIN_UNASSIGNED,  // batteryChargeStatus: none — charge state comes from the PMU
    1.0f,            // batteryDividerMultiplier: unused (no ADC path)
    PIN_UNASSIGNED,  // usbDetect: not verified; charge state comes from the PMU
    // CST836U: SDA39 SCL40 INT43 (open-drain, active-low, 10k pull-up on the FPC),
    // address 0x15, two points, no reset GPIO (FCA9555 P0.7 -> BoardReadPico).
    // MEASURED and derived from the vendor's own mapping. The reference firmware
    // feeds the CST836U frame straight in: UI_x = rawX, UI_y = rawY, in a
    // PORTRAIT 684x1216 logical frame (ui_kit.h:26-27, app_loop.c:196-197), and
    // reaches that frame from the 1216x684 panel scan via
    // epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT). So on this board the touch
    // frame is PORTRAIT: rawX (0..683) is the user's horizontal, rawY (0..1215)
    // the vertical, and the three key zones beyond rawY 1300 sit at the BOTTOM
    // edge, spread left/middle/right by rawX -- which is where they physically
    // are, and where the vendor's ui_key_hit_test looks for them.
    //
    // The reader runs Orientation::Portrait, whose GfxRenderer::tapToLogical is
    //   outX = panelHeight-1 - ny*panelHeight, outY = nx*panelWidth
    // (GfxRenderer.cpp:1995-1999). Reproducing the vendor's UI_x = rawX,
    // UI_y = rawY through that transform needs
    //   outY = nx*1216 = rawY  => the config's X axis must be rawY  => swapXY
    //   outX = 683 - ny*684 = rawX => ny must be 1 - rawX/683      => flipY
    // which is exactly swapXY = true, rawMaxX = 1215 (panel X), rawMaxY = 683
    // (panel Y), flipY = true. Substituting the four measured corners:
    //   raw( 54,  44) -> ( 53,   44)   raw(629,  64) -> (629,   64)
    //   raw( 41,1198) -> ( 40, 1199)   raw(632,1205) -> (632, 1206)
    // i.e. the mapping returns the raw coordinates, the vendor's 1:1 semantics.
    // flipX stays false.
    //
    // Two earlier attempts got this wrong and both are recorded here so they are
    // not repeated: swapXY alone (no flipY) transposes the axes, and reverting to
    // swapXY = false makes the mapping rotate 90 degrees -- which is what put a
    // top-bar tap onto a middle book row and opened the reader.
    {TouchController::Cst836u, 39, 40, 43, PIN_UNASSIGNED, 0x15, 0, 1215, 0, 683, false, 0, true, false,
     PIN_UNASSIGNED, true, false, true, false, true},
    NO_FRONTLIGHT,
    READ_PICO_AUDIO,  // AudioOutput::None; LEDC buzzer on GPIO2 (Buzzer lib)
    NO_LEDS,
    NO_FLIP,  // panel mount transform pending hardware; see docs/engineering/read-pico.md
    // 1-bit SDMMC: CLK38 CMD42 D0=44. D1/D2/D3 unused. No power gate and no CD
    // pin — card detect is FCA9555 P0.6 read by BoardReadPico.
    {38, 42, 44, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 1},
    // CW32L010 PMU at 0x2A on the shared bus SDA39/SCL40, 400 kHz: battery mV,
    // soc_permille and charge_state over the PMU frame protocol. No charger IC.
    {39, 40, 400000, 0x2A, 0, 0, GaugeType::Cw32L010Pmu},
    NO_MIC,
    // SC7A20H at 0x19 (WHO_AM_I 0x11). The CW32L010 (0x2A) also carries the RTC
    // and alarms, reached through the PMU protocol rather than a register map.
    {39, 40, 400000, 0x2A, 0, 0x19, 0, RtcType::Cw32L010Pmu, ImuType::Sc7a20h},
    1.2f,  // uiScale: touch device — STARTING VALUE, pending on-hardware measurement
    {},    // power: no latch, no charge enable — the PMU owns the host enable rail
    0,     // displayControllerVariant: not probed on this panel
    {5, 5, 8, 5},  // portrait TRBL: 8 px bottom, 5 px other edges
    false,         // default charge-status polarity
    NO_I2C_FRONTLIGHT,
    16             // native GL16 image output
};

static_assert(READ_PICO.grayscaleLevels == 16, "Read Pico exposes native sixteen-level image output");
static_assert(READ_PICO.displayWidth / 8 * READ_PICO.displayHeight == 103968,
              "Read Pico must use one 103,968-byte framebuffer (1216/8 x 684)");
static_assert(READ_PICO.displayController == DisplayController::LgfxEpd &&
                  READ_PICO.display.sclk == PIN_UNASSIGNED,
              "Read Pico has no SPI display pins; the parallel bus lives in LgfxEpdConfig");
static_assert(READ_PICO.sdmmc.busWidth == 1 && READ_PICO.sdmmc.d1 == PIN_UNASSIGNED,
              "Read Pico SD is 1-bit SDMMC");
static_assert(READ_PICO.touch.controller == TouchController::Cst836u &&
                  READ_PICO.touch.irq == 43 && READ_PICO.touch.irqActiveLow,
              "Read Pico CST836U INT# is GPIO43, open-drain active-low");

// Largest framebuffer (bytes): the one panel compiled into this build.
constexpr uint32_t panelBytes(const BoardProfile& p) {
  return static_cast<uint32_t>(p.displayWidth / 8) * p.displayHeight;
}
constexpr uint32_t MAX_FRAMEBUFFER_BYTES = FREEINK_DEVICE_READPICO ? panelBytes(READ_PICO) : panelBytes(EEGO_A4);

// Compile-time default device — the profile ACTIVE starts as. With a single
// device in the build this is the only device; with several same-MCU devices it
// is the boot default until the consumer calls selectDevice().
#if   FREEINK_DEVICE_EEGO_A4
constexpr BoardProfile DEFAULT_DEVICE = EEGO_A4;
#elif FREEINK_DEVICE_READPICO
constexpr BoardProfile DEFAULT_DEVICE = READ_PICO;
#endif

// Runtime-active profile. Defaults to DEFAULT_DEVICE — identical to the old
// compile-time behavior when only one device is in the build. A consumer that
// ships multiple same-MCU devices in one binary calls selectDevice() after its
// own hardware detection, before any pin is used.
inline BoardProfile ACTIVE = DEFAULT_DEVICE;

inline void holdPowerRails();  // defined below; used by selectDevice()

// Set ACTIVE to one of the devices compiled into this build. Returns false (and
// leaves ACTIVE unchanged) if `which` was not included via -DFREEINK_DEVICE_*.
inline bool selectDevice(Board which) {
  switch (which) {
#if FREEINK_DEVICE_EEGO_A4
    case Board::EegoA4:
      ACTIVE = EEGO_A4;
      break;
#endif
#if FREEINK_DEVICE_READPICO
    case Board::ReadPico:
      ACTIVE = READ_PICO;
      break;
#endif
    default:
      return false;
  }
  // Runtime-selected boards resolve after the consumer's first-thing-in-setup()
  // holdPowerRails() call (the dual X3+X4 binary boots with the X4 profile and
  // detects the real board here), so re-assert the selected board's latch pins
  // now that they are known.
  holdPowerRails();
  return true;
}

inline bool isM5StackPaperColor() { return ACTIVE.board == Board::M5StackPaperColor; }
inline bool isMurphyM3() { return ACTIVE.board == Board::MurphyM3; }
inline bool isDeLink() { return ACTIVE.board == Board::DeLink; }
inline bool isM5PaperV11() { return ACTIVE.board == Board::M5PaperV11; }
inline bool isM5PaperS3() { return ACTIVE.board == Board::M5PaperS3; }
inline bool isSticky() { return ACTIVE.board == Board::Sticky; }
inline bool isX4Pro() { return ACTIVE.board == Board::XteinkX4Pro; }
inline bool isX4Classic() { return ACTIVE.board == Board::XteinkX4Classic; }
inline bool isPaperMono() { return ACTIVE.board == Board::PaperMono; }
inline bool isEegoA4() { return ACTIVE.board == Board::EegoA4; }
inline bool isMetalioEInk4() { return ACTIVE.board == Board::MetalioEInk4; }
inline bool isMurphyM4() { return ACTIVE.board == Board::MurphyM4; }
inline bool isWaveshareEpaper397() { return ACTIVE.board == Board::WaveshareEpaper397; }
inline bool isOnePage() { return ACTIVE.board == Board::OnePage; }
inline bool isReadPico() { return ACTIVE.board == Board::ReadPico; }
inline bool isWsEpaper397() { return ACTIVE.board == Board::WsEpaper397; }
inline bool hasTouch() { return ACTIVE.touch.controller != TouchController::None; }
inline bool hasHomeKey() { return ACTIVE.touch.hasHomeKey; }
inline bool hasPwmFrontlight() { return ACTIVE.frontlight.gpio != PIN_UNASSIGNED || ACTIVE.frontlight.viaPm1Pwm; }
inline bool hasI2cFrontlight() { return ACTIVE.i2cFrontlight.controller != I2cFrontlightController::None; }
inline bool hasColorTemperatureFrontlight() {
  return (ACTIVE.frontlight.gpio != PIN_UNASSIGNED && ACTIVE.frontlight.gpioWarm != PIN_UNASSIGNED) ||
         ACTIVE.i2cFrontlight.controller == I2cFrontlightController::Lm3630a;
}
inline bool hasAudio() { return ACTIVE.audio.output != AudioOutput::None; }

// Safety guard: a power-latch pin must never coincide with a display or SDMMC
// bus pin. A latch is driven hard HIGH (asserted) or LOW (power-off) and held
// across sleep — if that pin is really, say, the display CS (GPIO13 on the X4
// Pro) or an SDMMC line, asserting the "latch" would clobber the bus. This
// catches a mis-set profile or an X4-vs-X4Pro config mixup before it drives the
// wrong pin. (The GPIO13 battery latch is correct on the C3 X4, where 13 is not
// a bus pin; on the X4 Pro 13 is the display CS, so a latch there is rejected.)
inline bool latchConflictsWithBus(int8_t pin) {
  if (pin < 0) return false;
  const DisplayPins& d = ACTIVE.display;
  if (pin == d.sclk || pin == d.mosi || pin == d.cs || pin == d.dc || pin == d.rst || pin == d.busy) return true;
  const SdmmcPins& s = ACTIVE.sdmmc;
  if (s.busWidth != 0 && (pin == s.clk || pin == s.cmd || pin == s.d0 || pin == s.d1 || pin == s.d2 || pin == s.d3)) {
    return true;
  }
  return false;
}

// Assert the board's power-rail latch pins. Battery-latched boards (e.g. the
// Sticky) must call this first thing in setup() or the board powers off when
// the user releases the power button. Releasing the pins (driving them LOW)
// is a software power-off. No-op on boards without a latch.
inline void holdPowerRails() {
  for (const int8_t pin : {ACTIVE.power.latch0, ACTIVE.power.latch1}) {
    if (pin < 0) continue;
    if (latchConflictsWithBus(pin)) {
      // Refuse to drive a bus pin as a latch — see latchConflictsWithBus().
#if defined(ENABLE_SERIAL_LOG)
      // esp_rom_printf, not Serial: this runs first thing in setup(), before
      // USB CDC enumerates, and consumers deprecate Serial.printf in headers.
      esp_rom_printf("[BOARD] power latch pin %d collides with a display/SD bus pin; skipping\n", pin);
#endif
      continue;
    }
    // A previous power-off may have latched the pin LOW with gpio_hold_en —
    // a state that survives a reset and a USB-powered deep-sleep wake, and
    // silently defeats the digitalWrite below. Release it first.
    gpio_hold_dis(static_cast<gpio_num_t>(pin));
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  // Charger enable (see PowerConfig::chargeEnable). Held with gpio_hold_en so the
  // level survives esp_sleep_config_gpio_isolate() and deep sleep (PowerManager
  // calls gpio_deep_sleep_hold_en() before sleeping) — the charger must stay
  // enabled whether the firmware is awake or asleep.
  if (const int8_t ce = ACTIVE.power.chargeEnable; ce >= 0 && !latchConflictsWithBus(ce)) {
    const auto g = static_cast<gpio_num_t>(ce);
    gpio_hold_dis(g);
    pinMode(ce, OUTPUT);
    digitalWrite(ce, ACTIVE.power.chargeEnableActiveHigh ? HIGH : LOW);
    gpio_hold_en(g);
  }
}

// Rescue the SD power rail before first display use. A previous firmware's
// sleep path may have latched the rail off with gpio_hold_en — a state that
// survives reset and reflashing — and on boards where SD shares the display's
// SPI bus an unpowered card clamps SCLK/MOSI so the panel never hears a
// command. Releases the hold, powers the card, and deselects its CS.
// SDCardManager::begin() does this itself; apps that skip SD should call this
// once before display.begin(). No-op on boards without a switched SD rail.
inline void releaseSdRail() {
  if (ACTIVE.sd.powerEnable >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(ACTIVE.sd.powerEnable));
    pinMode(ACTIVE.sd.powerEnable, OUTPUT);
    // Drive the enable to its ON level: HIGH for active-high rails, LOW for the
    // active-low ones (X4 Pro's GPIO5 powers the card while held LOW).
    digitalWrite(ACTIVE.sd.powerEnable, ACTIVE.sd.powerActiveHigh ? HIGH : LOW);
  }
  if (ACTIVE.sd.cs >= 0) {
    pinMode(ACTIVE.sd.cs, OUTPUT);
    digitalWrite(ACTIVE.sd.cs, HIGH);
  }
}
inline bool hasMic() { return ACTIVE.mic.input != MicInput::None; }
inline bool hasBuzzer() { return ACTIVE.audio.buzzer != PIN_UNASSIGNED; }
inline bool hasRtc() { return ACTIVE.sensors.rtcAddr != 0; }
inline bool hasTempHumidity() { return ACTIVE.sensors.tempHumidityAddr != 0; }
inline bool hasImu() { return ACTIVE.sensors.imuAddr != 0; }
inline bool hasLeds() { return ACTIVE.leds.data != PIN_UNASSIGNED && ACTIVE.leds.count > 0; }

}  // namespace BoardConfig
