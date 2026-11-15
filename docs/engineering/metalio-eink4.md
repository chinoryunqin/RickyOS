# Metalio E-Ink 4

Independent ESP32-S3 firmware target, model/board tag `metalio_eink4`, public
Nightly slug `metalio-eink4`. Hardware reference: `metalio-hw-test` 2.0.51,
`main/hal/metalio-e-ink-4/config.h`, `IOExpander.hpp`, and its SSD1677 driver.
The port was developed on SDK `5faf69e8` and rebased onto the current CrossMux
SDK dependency, including its SD-capacity fix; it does not migrate the reference's
ESP-IDF/LVGL application or add its audio, cellular or IMU features. Global touch
haptic feedback is supported as described below.

```sh
pio run -e metalio_eink4
CROSSPOINT_RC_HASH=$(git rev-parse --short=7 HEAD) pio run -e metalio_eink4_nightly
pio run -e metalio_eink4 -t upload --upload-port <verified-metalio-port>
pio device monitor --port <verified-metalio-port> --baud 115200
python3 -m unittest discover -s scripts/tests -p 'test_metalio_eink4.py'
python3 freeink-sdk/libs/hardware/BoardConfig/test/host/test_metalio_charger.py
```

The target uses the existing 16 MiB flash dual-OTA partition layout and the
prebuilt S3 TinyUSB core with octal PSRAM. Actual PSRAM capacity is detected at
boot. First installation must include CrossMux's bootloader/partition table;
do not place only the OTA application into the hardware-test firmware's layout.
The Nightly full-install manifest supplies the matching offsets and files.
Both legacy flavor manifests point to the same unified firmware. Tagged images
for other boards are rejected by the existing flash/OTA board-tag check.

## Hardware contract

| Function | Wiring / behavior |
|---|---|
| Display | GDEM0397T81, SSD1677, native 800×480; SCLK14 MOSI8 CS45 DC13 RST18 BUSY9, 10 MHz |
| SDMMC | 1-bit, CLK38 CMD40 D0=39; GPIO46 DAT3/CD input pull-up only |
| Shared I²C | SDA41 SCL42, 400 kHz, Arduino Wire transaction locking |
| TCA9555 | `0x20`, INT2 input pull-up; MAIN=P0.6, SCREEN=P0.5, touch reset=P1.1, shutdown pulse=P1.3 |
| Touch | CST816S `0x15`, active-low IRQ1, reset through the expander |
| Buttons | BOOT0=Confirm, POWER3=Power; P0.7=Down/next, P1.0=Up/previous, active-low |
| Virtual keys | Raw Home=(80,900) unchanged; Prev=(400,900) → Left, Next=(240,900) → Right |
| Battery | BQ27220 `0x55`: percentage and gauge-native charging status |
| Charger | Optional CX25601N `0x6B`: fixed charge parameters at boot; status is never interpreted as BQ25896 |
| RTC | PCF8563 `0x51`, existing system-UTC restore/writeback behavior |
| USB | Native USB19/20, Serial/JTAG and existing USB Drive/MSC workflow; TCA9555 P0.0 held high for flash/debug routing |
| Haptic | GPIO44, active-high motor; Off / Low 20 ms / Medium 35 ms / High 60 ms |

The expander preloads safe output levels before setting directions. Main power
is asserted before screen power; touch reset is held low for 10 ms and settles
for 120 ms after release. P0.4 amplifier, P0.1 amplifier routing and GPIO44
motor start low. P0.0 USB routing is preloaded high before enabling its output.
Unknown expander lines stay inputs.

## Display, input and shutdown

Metalio uses the shared SSD1677 driver with board-specific parameters. Explicit
FULL is `0xF7`; ordinary B/W/image FAST is `0xFC`. Text AA uses the custom
single-activation path documented [below](#text-aa-after-the-2026-09-28-review).
Initial drawing, periodic HALF and
physical grayscale cleanup use two FAST phases: establish black from a white
previous plane, then paint the target from black. Each phase finishes before
RAM changes, and final BW/RED planes match the target. The BSP's HALF `0xD7` /
`0x6A` parameters remain in the board configuration but are not the default
HALF cleaning path. Border is `0x01` for FULL and `0x80` for partial/gray/park.
Power-off uses `0x83`, then deep sleep `0x10/0x03`.

The driver separately records physical grayscale residue and whether RED holds
a synchronized B/W baseline. The HAL's `DisplayRefreshContext` is passed with
each synchronous, asynchronous or grayscale-base request; `Normal` is the
backward-compatible default. `ContinuousReading` allows only a FAST request
with a synchronized baseline to reuse it. It cannot bypass initial drawing or
periodic cleaning. Reader helpers supply this context for text pages and XTC;
menus, image apps and sleep screens use the normal cleanup contract. There is
no persistent next-call permission. The SDK facade uses optional `WithContext` hooks whose default implementations
call the original driver methods. Only SSD1677 overrides these hooks; other
drivers require no signature or source changes.

One driver decision selects FAST, HALF, FULL or black-pulse cleaning for the
full-frame and deferred paths; window updates escalate to the same full-frame
clean when needed. Cleans complete synchronously; ordinary FAST retains async
overlap. BUSY timeouts keep the baseline unknown, prevent further RAM/sleep
commands while busy, and do not mark the controller powered off. `0xCC` leaves
analog power on, so shutdown still performs the required park sequence.

The BlackPulse cleanup changes retain the LUT, voltage, SPI rate and discharge
delay. Constant fills use
the existing 128-byte stack chunk; grayscale retains strip rendering and its
board-configurable LUT. No additional full-screen buffer or polling task is
added. The framebuffer is 48,000 bytes; SDMMC retains its existing 4 KiB DMA
bounce buffer. Static sizes do not establish the runtime memory budget.

```sh
python3 -m unittest discover -s scripts/tests -p 'test_metalio_display.py'
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677.py
# Optional command tracing; black-pulse cleaning is already the board default.
PLATFORMIO_BUILD_FLAGS='-DSSD1677_PROBE_DEBUG=1' pio run -e metalio_eink4
```

The earlier A/B flag and FULL-after-gray trial policy have been removed.
Record image SHA-256, waveform BUSY time, visible transition, park and shutdown
pulse time separately. Ten gray/menu/sleep/wake cycles and 1/10-minute retention
checks remain part of physical acceptance; explicit FULL is the quality
comparison. The user confirmed the page-turn issue resolved and selected the
running black-pulse behavior for Nightly; this is not full ghosting acceptance.

The touch backend decodes the five-byte frame at register `0x02`; the ISR only
sets a pending flag. Held contacts are sampled on the existing 8 ms cadence,
idle contacts wait for IRQ, and I²C failures back off for two seconds.
Expander keys share one read per 20 ms. Failed reads cancel held input, never
synthesize a Home click, and recover when communication resumes and contact
returns idle. Missing touch leaves BOOT, Power and side navigation available.

Virtual keys are classified before coordinate mapping; other out-of-range
coordinates are discarded. Screen points map to `(rawY, 479-rawX)`, then pass
through the normal orientation transform. Crossing from screen to bezel keys
cancels that contact until release. Home short press returns Home; a 700 ms
hold uses the existing reader-menu action and consumes the subsequent short
release. Bezel Prev/Next now report Left/Right through the existing front-button
mapping; physical side buttons report Down/Up respectively and retain user
side-button swaps. Activity transitions retain input suppression. This key-map
correction follows hardware feedback. The user confirmed the corrected key mapping
on the physical device; this does not extend the earlier display-retention or
power-cycle acceptance.

The initial held Power gesture and its release are consumed on every boot.
Subsequent Power gestures and automatic sleep use existing settings. Shutdown
saves reading state, renders the sleep screen and parks the controller before
continuously sending 100 ms high/100 ms low power-key pulses until hardware
removes power, matching the reference board. MAIN/SCREEN rails stay enabled and
PA stays off. There is no three-pulse limit or ESP deep-sleep fallback, including
when USB maintains supply. Initialization retries every second; failed pulse
writes retry at the same cadence with error logs limited to once per second.
USB diagnostics remain available. Shutdown does not reconfigure the charger.

CX25601N external-power status is cached for one second. Absent/unreadable
chargers retry after two seconds and fall back to USB SOF activity and the
existing gauge charging indication. On legacy boards, a full battery connected
to a charge-only source may not be distinguishable from disconnected power.
On boot, after the shared I2C bus starts, the HAL passes its fixed policy to
the SDK charger interface, which probes CX25601N register `0x38`. If present,
the SDK disables charging while setting VREG to 4350 mV, IPRECHG to 240 mA,
ITERM to 60 mA, ICHG and IINDPM to 480 mA (the chip's
80 mA step rounds a 500 mA request down), then enables hardware termination
and charging with HIZ and the watchdog cleared. A missing chip or failed I2C
operation is logged without blocking boot; failure after charging is disabled
leaves it disabled to avoid charging with partially updated parameters.
This policy assumes a battery rated for 4.35 V charging. It does not change
DPDM detection, private registers, the existing status path, or add a
dynamic-regulation/recharge task. It has not yet been physically accepted.

## Acceptance

Automated checks and hardware acceptance are separate. Use the checklist below
for complete acceptance; the dated session records only the observations made:

- Cold boot, reset, completely remove/reapply power; boot logs identify
  `metalio_eink4`, PSRAM, SD mount and RTC/gauge status without panic/OOM.
- FULL/HALF/FAST, window updates and four-gray reading; verify orientation,
  corners, AA and residual image after shutdown and an extended idle period.
- All physical/virtual keys, short/hold/release, rapid page turns, long Power
  held across boot, and contacts crossing activity/popup boundaries.
- Open an EPUB from SD, turn pages, save progress/settings and verify after
  reboot. Missing SD must use the existing recoverable SD-error screen.
- Set RTC, reset and remove power; verify UTC recovery. With CX25601N, read
  back VREG/ICHG/IINDPM/IPRECHG/ITERM/EN_TERM/EN_CHG, then measure battery
  current and charging state on plug/unplug, full charge, and reboot. Confirm
  an older board without CX25601N still boots and reports power as before.
- USB MSC copy/rename/delete/large-file read; eject/cancel/disconnect and verify
  reboot, SD remount and opening the transferred book. Repeat three times.
- Existing Wi-Fi transfer/OTA and BLE page turner connect/disconnect/reconnect.
- Log internal free heap, historical minimum, largest free block and PSRAM
  before/after reading, BLE, Wi-Fi, USB transfer and sleep cycles. Confirm no
  downward trend; record cold boot/reset/repower independently.

Local build, local tests, hosted CI, public release, flashing and physical
acceptance must each be recorded independently.

### 2026-09-12 local flash session

- Target identified as ESP32-S3 revision 0.2, 16 MiB flash, embedded 8 MiB
  PSRAM; the previous firmware logged the reference CX25601N driver.
- Flashed `metalio_eink4`, version `1.5.8-metalio-eink4-rc+c2fb3467`, from
  the uncommitted implementation. Bootloader at `0`, partitions at `0x8000`,
  boot_app0 at `0xe000`, application at `0x10000`; all four esptool hash
  checks passed. No full-chip erase. The user already had a backup, so the
  additional backup attempt was stopped before flashing.
- Application SHA-256:
  `beb43a5b27003ca6c0bbba01598695cda80340949ce2cab28fa84ce6d4555b42`.
- First boot reached language selection. Subsequent interaction logs showed
  menus, settings, keyboard input, Wi-Fi connection and HTTPS image downloads
  (96,070-byte BMP and 198,363-byte JPEG), SD cache writes and grayscale
  rendering. Full refresh completed in about 3.3 s, ordinary refresh in
  386 ms, and the observed grayscale phase in 143–145 ms. These are controller
  completion logs, not visual confirmation of waveform quality or touch accuracy.
- A subsequent device restart remounted SD, restored settings, initialized
  the display and reconnected with saved Wi-Fi credentials. Its physical reset
  cause was not captured; the host reset command coincided with USB absence
  and did not execute. Do not count this as a controlled cold-boot/repower test.
- SNTP completed and the external RTC write/readback was verified. RTC
  recovery after removing power remains untested.
- Internal heap: language page free 204,456 B, largest block 155,636 B;
  Wi-Fi keyboard free 157,972 B, largest 114,676 B; image session minimum
  free 143,064 B, largest observed block 106,484 B. After restart, menu free
  204,760 B, largest 155,636 B. Runtime PSRAM usage was not logged.
- No captured panic, OOM or CST816S/TCA9555 communication failure. One Arduino
  `disconnect(): STA not started` message preceded a successful saved-network
  connection; this was not a connection failure.
- Still pending: user confirmation of display quality, four-corner touch and
  every physical/virtual key gesture, controlled reset/cold boot/complete
  repower, shutdown retention, EPUB progress, USB MSC, BLE, battery/USB changes
  and extended memory measurements. Hosted CI and publication were not run.

### 2026-09-12 refresh and reading regression session

- The first state-only test firmware incorrectly cleaned after every text-AA
  page. The user rejected this behavior. The reader baseline reuse
  fixes this without changing the saved refresh frequency.
- The reader fix was flashed first with the default cleaning policy (SHA-256
  `aa764e5a02b418c0e276a0185d1210bf7a05e12ec3f4bb5fb91f982b96a84f89`),
  then with the optional black-pulse policy and command tracing (SHA-256
  `588c50a9ab4ee41183191bb80c9139f723e5f2a2ffcbf36856744f2a78038ca9`).
  Both application writes passed esptool hash verification. The second image
  initially produced no captured serial output; after reconnecting, live
  reading logs confirmed that it was running. This is not a controlled boot test.
- The live black-pulse session captured consecutive text pages 1 through 14
  using one FAST `0xFC` activation plus the `0xCC` AA phase per page. Typical
  complete page rendering was 0.77–0.85 s; some pages took 1.32–1.44 s with
  longer CPU/render/write phases. Async activation's reported 0–3 ms is only
  submission time, not the panel's refresh duration.
- Clean transitions used two `0xFC` phases, each about 385–386 ms BUSY time;
  the complete B/W display call took about 1.09 s. Grayscale BUSY time was
  about 143–148 ms. No BUSY timeout or panic appeared in this capture. These
  measurements do not establish visual ghosting quality or total shutdown time.
- Internal free heap ranged from 82,488 to 168,896 B in the captured reading
  samples; historical minimum was 44,404 B and largest blocks ranged from
  31,732 to 73,716 B. These samples are not a long-duration leak test; runtime
  PSRAM usage remains unmeasured.
- Local checks passed: 47 Python tests, including real-driver command traces
  and the shared reader cadence harness, plus 24 SDK input checks. Physical
  visual acceptance and ten gray/menu/shutdown/startup cycles with 1/10-minute
  retention checks remain pending. At that stage the black-pulse policy remained opt-in; the subsequent
  approved refactor makes it the board default.
- Rebuilt `gh_release` (unified X3/X4), `waveshare_epaper_397`,
  `metalio_eink4` and `metalio_eink4_nightly`: all four passed. The rebuilt
  default Metalio image has SHA-256
  `fc800a9f3d342ccbb427345b61de3e51409814098ab83d58568b4796df512ecc`;
  it was archived locally, not flashed over the running black-pulse test image.
  No hosted CI, commit or public release was performed.

### Refactor and PR delivery

- The user confirmed the page-turn regression resolved and selected black-pulse
  cleaning as the board default for PR/Nightly delivery. This does not replace
  the pending long-retention and power-cycle checklist.
- Replaced the one-call permission with request-scoped reading context; kept
  physical residue pending even after a FAST reading page without a new gray
  overlay. Full/window/deferred paths share the clean decision, and BUSY
  failures cannot commit a false off state or continue writing RAM.
- Removed the temporary FULL-after-gray policy and opt-in black-pulse flag.
  Tests now compile the real SDK driver and include `ReaderRefresh.h` directly,
  with no production-source string extraction. Trace coverage includes both
  failed black-pulse phases, failed analog park, deferred completion, explicit
  FULL and leaving reading without another gray overlay.
- Rebased onto CrossMux main `fd4a74af` and its SDK dependency `8242f43`, which
  includes the existing SD-capacity repair (SDK PR #25). That dependency must
  land before the Metalio SDK PR; the firmware PR pins the pushed SDK revision.
- Local Python checks: 53 passed. SDK input checks: 24 passed, plus the touch
  activity host checks. Post-refactor firmware builds, package hashes, serial
  reflash and PR CI outcomes are recorded separately below as they complete.

### Driver compatibility scope

The original `PanelDriver::display`, `displayStart`, and `displayGrayscaleBase`
interfaces are unchanged. The three `WithContext` hooks are optional extensions;
legacy drivers retain their existing virtual dispatch, including specialized
async and grayscale-base behavior. The 22 files belonging to the other 11 panel
drivers match SDK dependency `8242f43` byte for byte. The Metalio policy remains
inside the configurable SSD1677 driver, with no duplicate platform driver.

A host compatibility test implements only the original interface and verifies
all three new hooks dispatch to it. The facade also forwards reading context
when an inverted grayscale-base request falls back to ordinary display.

### Key mapping validation (2026-09-12)

- Real `InputManager.cpp` host coverage verifies BOOT confirmation, separate
  expander directions, bezel Left/Right, failed-read release, and unchanged Home
  tap/hold events. Existing 24 SDK input checks also passed.
- Both Metalio build environments passed before rebasing onto the latest main.
  The tested application SHA-256 was
  `0a76680541efebd688a935ab926fad33173c1c2b4ac5fda19d3db23ac931a7a6`.
- The initial app0 write verified successfully but the device still selected
  app1. Reading the actual partition table and OTA metadata identified app1 at
  `0x650000`; rewriting that slot took 51.1 seconds and passed hash verification.
  Boot logs confirmed the expected build, SDMMC, RTC and saved settings.
- The user confirmed the corrected key mapping. Future serial updates must
  inspect the selected OTA slot; write verification alone does not establish
  that the new image booted. App-menu logs also reported a drawing-boundary
  warning; it is separate from this mapping change and was not addressed here.


## 2026-09-16 reference audit and haptic feedback

Reference: `/home/zzb/workspace/ink/Metalio-E-INK4`, particularly
`main/boards/metalio-e-ink-4/config.h`, `metalio_e_ink_4_board.cc`,
`metalio_touch.c` and `main/boards/common/IOExpander.hpp`.
These are software wiring evidence, not a new schematic or physical acceptance.
Workspace bases: CrossMux `d0889e7224c0`, SDK `e8e0276d0609`, plus local
uncommitted changes; the SDK gitlink is unchanged.

| Subsystem | Comparison and decision |
|---|---|
| SoC / storage | Retain independent S3 N16R8 target, existing dual-OTA layout, SDMMC CLK38/CMD40/D0=39 and input-only DAT3=46; do not copy the reference application partitions. |
| Display | Panel, 800×480 geometry, SPI pins and 10 MHz agree. Retain the physically selected CrossMux SSD1677 cleaning policy and grayscale lifecycle. |
| Touch / keys | Native coordinates, three bezel locations, GPIO1 IRQ, expander key pins and 10/120 ms boot reset agree. Add `0xA5=0x03` before deep sleep; failures log and still allow shutdown. Reset on boot restores touch after deep-sleep wake. No new light-sleep policy. |
| Power / expander | Keep safe latch preload and main/screen rail ordering; repeat 100 ms high/low shutdown pulses until power is cut, without an ESP deep-sleep fallback. Add P0.0 output HIGH for USB flash/debug routing, matching the reference FSUSB42UMX selection. |
| Haptic | Reference GPIO44 active-high, 35 ms timer pulse. Use board-calibratable 20/35/60 ms feedback for the entire touch surface and capability-gated settings. |
| Gauge / charger | BQ27220 at 0x55 and optional CX25601N at 0x6B agree. At this audit, charger control was intentionally deferred; the current boot-time configuration above supersedes that decision. |
| RTC | Retain PCF8563 at 0x51 and system-UTC restore/writeback. |
| Audio / microphone | Reference external BT audio module uses UART TX48/RX47 and I²S BCLK6/WS43/DOUT7/DIN17. CrossMux leaves audio/mic capabilities disabled and PA off; codec/module control needs a separate port. |
| IMU | Reference SC7A20H at 0x19, interrupt on TCA9555 P1.4. CrossMux keeps IMU disabled; no substitute QMI8658 driver or automatic rotation is enabled. |
| Cellular | Reference NT26 UART TX12/RX11, MRDY21/SRDY5. No CrossMux modem service is introduced. |
| USB camera | Reference switches P0.0 LOW for camera host use. CrossMux keeps HIGH for existing native-USB debug/MSC; no UVC host driver. |
| Wi-Fi / BLE | Retain existing CrossMux Wi-Fi and BLE page-turner paths, not the reference voice/network application. |

`FREEINK_CAP_HAPTIC` defaults to Metalio only and can be overridden with
`-DFREEINK_CAP_HAPTIC=0`. Unsupported boards remain at zero; enabling it on a
new board requires a motor pin/calibration implementation. The System setting
`hapticFeedbackLevel` appears immediately after the sound-feedback slot, even
when sound is unavailable. Values are Off=0, Low=1, Medium=2, High=3; missing or
invalid stored values use Medium. English/Chinese labels use the existing i18n
pipeline. Persistence and web exposure use the existing settings registry.

One accepted contact anywhere on the touch surface (screen, including blank
areas, or Home/Previous/Next) emits one press event before semantic gesture
classification. Swipes and long holds vibrate once at initial contact; motion,
release, physical buttons, Bluetooth, cancelled contacts and suppressed activity
transitions do not add pulses. An active pulse drops new requests rather than queuing/extending
vibration. Turning feedback off stops the motor before settings save/redraw;
the main loop also applies Off. Sleep stops it before panel parking and again
at the HAL input shutdown boundary, which holds GPIO44 LOW through deep-sleep
GPIO isolation. Board initialization releases that hold even in a capability-off
build. Light sleep is prevented from replacing the motor output configuration.

The SDK motor driver owns one `esp_timer`, allocated once at initialization and
reused until reset. The platform API has no static timer allocation alternative;
its timer task already exists, so no application task/stack is added. The local
Arduino 3.3.7 S3 `libesp_timer.a` disassembly shows a 32-byte timer payload
(`esp_timer_create`, calloc arguments 1×32), excluding allocator overhead; this
is version-specific, not measured heap use. Static state is one handle, one
critical-section lock and an int64 deadline. No per-pulse heap allocation or
framebuffer is added. The settings row uses the existing cold-path registry
allocation for four uint16 labels (8-byte payload plus allocator/row overhead);
incapable builds compile that row/field out.

GPIO changes and expiry checks share a critical section. A deadline check makes
an already dispatched old callback harmless after stop/restart. Allocation or
start failure leaves the motor off and logs the failure. Timing remains subject
to the shared ESP timer task's scheduling latency; physical pulse duration and
motor startup at Low require hardware validation.

### Hardware shutdown

Long-press shutdown and automatic sleep share the same Metalio HAL path. After
saving state, rendering the sleep image, stopping radios/haptics and parking the
display/touch/storage, SDK `shutdown()` continuously drives P1.3 high for 100 ms
then low for 100 ms until hardware removes power. MAIN/SCREEN remain enabled,
PA remains off, and USB diagnostics remain available. Initialization retries
every second; pulse write errors retry at the original cadence with rate-limited
logs. The SDK interface is now `[[noreturn]] void`; no GPIO-wake deep-sleep
fallback is compiled into the Metalio branch. Other devices are unchanged.

The user reported battery exhaustion after about two days following battery-only
long-press shutdown. The previous three-pulse fallback could leave peripheral
rails powered during MCU deep sleep; this is a suspected mechanism, not a
measured root cause. The reference also continuously pulses until power is cut.
A hardware fault or USB supply can keep this loop running indefinitely.

### Validation and physical evidence

- All 55 script checks passed, including the real InputManager, contact edges,
  screen/bezel hold/swipe/release and cancellation, motor timer failures/races,
  capability-disabled compilation, USB preload, touch-sleep command and real
  reader/SSD1677 cadence. Shutdown tests simulate more than three pulse cycles,
  permanent/transient I2C failure and initialization failure/recovery.
- Final ordinary Metalio build passed and was flashed to the user's ESP32-S3
  revision 0.2, MAC `10:20:ba:6e:08:70`, via `/dev/ttyACM1`. Read-back partition
  table matches the build; valid OTA sequence 2 selects app1 at `0x650000`.
  Wrote 6,060,080 bytes in 51.8 seconds; esptool hash verification passed. No
  settings/data partition or SD data was written.
- Tested ordinary application SHA-256:
  `73a430141b0a31da0d86c35934c01c51109aef074021ff3c141f56a5390f64f3`.
  Post-reset main-loop heap free/minimum/largest block was
  199,576 / 199,320 / 155,636 B, without observed panic/OOM in the short capture.
  This is not a sustained heap or battery-current measurement.
- Earlier shutdown capture showed automatic sleep after 600,000 ms, entry into
  the continuous pulse loop, a brownout message and serial disconnection. The
  user confirmed subsequent power-on was manual, not an observed spontaneous
  restart. This does not establish sustained rail-off or a low battery current.
- On 2026-09-17 the user reported testing complete and requested the related
  PRs. No quantitative current, rail-voltage, physical pulse-width or exhaustive
  sleep-cycle measurements were supplied; those remain unmeasured.
- The attempted desktop-entry FULL override was withdrawn after user feedback.
  It is absent from the final changes: original desktop refresh, reading cadence,
  antialiasing and SDK display waveforms are retained.
- Final Nightly and repository CI results are recorded with the PR handoff.

The SDK changes were integrated through
[freeink-sdk#29](https://github.com/0x1abin/freeink-sdk/pull/29). The application
pins merged commit `094976e1d47ad7120cf461fec5f6b737eaabf13f`; its source tree
`fa514adeac892085ec6205645c153b1f6dad4956` exactly matches tested feature commit
`4512f1441e1ab54dea8d9889cc35e021991413c8`. No SDK source or build-relevant file
changed during integration. [CrossMux PR #318](https://github.com/0x1abin/crossmux/pull/318)
contains the application/HAL changes. No application merge or firmware publication
is part of this work.

## 2026-09-25 combined text AA recovery

The identified ESP32-S3 unit (USB serial and MAC `10:20:ba:6e:08:70`) booted
from CrossMux app0 at `0x10000`. Its earlier default-AA image entered the home
activity and logged completed refreshes, but the user reported that opening a
book did not work. The shared display fix waits for an outstanding asynchronous
refresh before routing and resets a sleeping SSD1677 before checking BUSY.

Only app0 was replaced with `1.6.0-metalio-aa-fix1` (SHA-256
`99f56c7792832d1fb809aec19fbedd38d05b352161d96f2736c814c3942d5397`);
`esptool verify-flash` matched. The SD card mounted and the main loop ran with
no captured panic or BUSY timeout. The user then reported that book opening was
restored. This does not measure gray tone, residual ink or 100-page EPUB/TXT
performance. The subsequent code review also consolidates controller wakeup
paths; that revision still needs physical retesting.

## Text AA after the 2026-09-28 review

Normal and Nightly Metalio builds now use the endpoint-preserving text-turn
core described in [SSD1677 text AA](ssd1677-text-aa.md). The user confirmed that
the tested `g24-b32-w32-d0-khold` candidate meets synchronous AA expectations
and resolves fading status text, guides and body strokes. Its timing and
analog settings are retained. `metalio_eink4_transition_experiment` remains a
diagnostic build of the same behavior; the multi-stage experimental flags
and duplicated Metalio update function are retired.

The final selector policy refreshes all target black and white pixels on a
changed page and keeps static gray idle. Identical pages do not drive. One
shared submission flow handles power settling, cancellation, BUSY failure and
committed glass state; Metalio still parks with `0x83`. First entry, images,
inversion, manual/periodic cleanup and recovery retain their safe paths.
The existing eight PSRAM planes are reused without per-page allocation.

The comparison images and hashes remain local under
`build/metalio-aa-calibration/`, including the accepted candidate
`w32-d0-khold/firmware.bin` (5,935,232 bytes; SHA-256
`cf5649adbf31e4333f67dd693e52510559d397de9f9b429c3b2e545ada3553d1`).
It was written only to verified Metalio MAC `10:20:ba:6e:08:70`, active app1
at `0x650000`, with independent digest verification and startup confirmation.
The consolidation itself is verified by host traces and builds; no additional
flash is performed for publication. Detailed recorded 100-page/video acceptance
remains pending. The shared document carries current test and rollback commands
and the default rollout on Sticky, both SSD1677 Murphy M4 batches and Waveshare
3.97. Those panels' independent optical acceptance remains pending.

### 2026-11-06 ordinary E-Ink4 reading candidate (dev20)

The `codex/metalio-eink4-1.2.11` branch at `945380e9` is the base for the
`1.2.17-metalio-dev20` candidate. The ordinary panel previously
replayed each grayscale plane in six 80-row bands. This means twelve page
traversals for the two gray planes, in addition to the B/W and prewarm passes.
The synchronous text-AA backend normally takes that scratch-strip path.

`EpubReaderActivity::onEnter()` now owns one physical gray plane in PSRAM
(100-byte stride × 480 physical rows = 48,000 bytes). It renders each plane
once and uploads it through the existing HAL strip API, reusing the buffer for
the second plane and subsequent pages. The B/W framebuffer remains intact.
The allocation is too large for the render stack; PSRAM avoids consuming
internal/DMA RAM. Allocation failure retains the existing strip fallback, and
`onExit()` releases the plane. The buffer and full-plane rendering are compiled
only for ordinary E-Ink4. No SDK waveform, cleaning cadence or C3 buffer budget
is changed. Less repeated CPU work is the expected mechanism; elapsed-time
improvement has not been measured.

The production `ReaderGrayPlane.h` helper is checked against the previous strip
traversal for both planes and four orientation patterns, including canaries and
B/W preservation, under ASan/UBSan:

```sh
python3 -m unittest scripts.tests.test_reader_gray_plane
python3 -m unittest scripts.tests.test_ssd1677_text_aa
```

Both commands passed. The full `rickyos_metalio_eink4` build passed: firmware
6,406,224 bytes, app-slot headroom 147,376 bytes, SHA-256
`2ba853e1a2b494ef4cb2893b6e44bbc8ef6446df7c41a05668cf72a422436c5d`.
The SDK overlay check passed for all 104 paths. The two
legacy `test_metalio_display.py` sequence tests fail assertions with the synced
SDK overlay; none of their input files were changed by this candidate. The
repository-wide `./bin/ci-check` stops at uninitialized unrelated submodules.
These are incomplete checks, not a full CI pass.

Hardware acceptance is pending: use the same EPUB, font and size; turn at least
20 text pages forward/backward, cross a chapter, open an image page, return to
the menu and reopen the book. Keep the normal periodic clean setting. Check
rapid input, gray/menu residue and saved progress after reboot. Serial
`Page render (buffered gray)` reports stage timing, while `MEM` lines provide
internal heap and PSRAM observations. Compare repeated sessions for continuing
memory decline and compare elapsed times with the prior firmware before
claiming a performance gain.

Initial USB ROM handshakes timed out. After the user woke/reconnected the unit, the expected ROM MAC `10:20:BA:6D:6C:CC` and 16 MiB flash were verified.
A complete current-flash backup was read and verified by the ROM tool before
writing, SHA-256
`d8a480e8c77139e389422089d5df6bdce010330f130335bcd22a9abe367f6d92`.

The actual pre-install partition table differed from the handoff: NVS at
`0x9000/0x6000`, OTA data at `0xf000/0x2000`, app at `0x20000/0x400000`,
and separate assets/storage. Its app descriptor was `eego`,
`b99de7cf-dirty`. The 4 MiB slot cannot hold this build, so application-only
installation was rejected before writing. The matched full-install image uses
`partitions.csv`: bootloader `0`, table `0x8000`, OTA initializer `0xe000`,
application `0x10000`. Its padding initializes the new NVS area; old internal
system/settings are recoverable from the verified full backup. The flasher
does not access the SD card. Full image: 6,471,760 bytes, SHA-256
`2a7e28354abffa44545c7bfc16988ff73906e8a078e5feda2a4886cc813acd41`.
The full installation and ROM digest verification passed. Independent readback
of all 6,471,760 bytes matched the full-image SHA-256. The new firmware booted
as `1.2.17-metalio-dev20`, detected `metalio_eink4`, mounted SDMMC and entered
`InxRecent`. At 10/20 seconds the internal free heap was 128,428 bytes with an
86,004-byte largest block; PSRAM free was 5,060,156 bytes with a
4,980,724-byte largest block. These are idle boot snapshots, not a repeated
reading/leak test. The known missing-IMU log and initially absent NVS namespaces
were observed. No panic, abort or heap-corruption marker was observed in this
initial boot capture. Reading latency and visual acceptance remain pending.

Local artifacts and verified backup:
`/Volumes/Seagate/RickyOS-device-backups/metalio-dev20-20261106/`.
Frozen install artifacts:
`/Volumes/Seagate/RickyOS-build-output/metalio-dev20-20261106/`.
User feedback after installation: slightly smoother, but still sluggish and
not following input closely. This candidate is not a completed performance fix.
The subsequent serial capture confirmed a responsive main loop and a reader
framebuffer, but captured no active page-turn timing records. Do not use those
idle samples to attribute the remaining latency. Capture actual turns and compare
`prewarm`, `bw_render`, `gray_render`/`gray_write`, and `gray_display` next.

A source candidate for input latency is the synchronous next-page font prewarm
in `EpubReaderActivity::loop()` before page-turn dispatch. It runs only for
eligible SD fonts, so first confirm the active font and measure it; this
investigation has not changed that path. SDK sources still match the existing
104-path overlay, and the pinned SDK gitlink is unchanged.

### 2026-11-06 dev21 measured reading and menu latency

Active dev20 page turns were captured on the connected ordinary device.
Across 11 ordinary turns, total time ranged from 1,226 to 1,681 ms, with a
1,399 ms median. B/W painting accounted for 659–1,074 ms, median 809 ms.
Prewarm was 0–1 ms, each gray-plane render 70–98 ms, the second-plane
render/upload 81–109 ms, and gray display 406–408 ms. The first cold page took
3,289 ms. Returning from the menu to the already rendered page took 1,191 ms.
These measurements identify cold painting as a reading target and do not
support removing the synchronous idle prewarm based on this session.

The reader streams the selected 32 MB TTF from SD. dev21 enables the existing
Plus streamed-tail block cache on ordinary E-Ink4 as well. Each streamed style
allocates 32 × 4,096 bytes in PSRAM on first use, with checked allocation and
read-through fallback. All UI and reader faces borrowing that source share
the blocks. Source teardown frees the blocks. The maximum for four actual
streamed styles is 512 KiB plus the small index arrays. This avoids repeating
small card reads while FreeType requests nearby pieces of one outline.

The retained UI selector conversion now uses two compile-time 256-byte tables
instead of repeatedly quantizing the same pixels for each uploaded plane.
Committed history packing uses the same pair conversion. Unaligned selector
requests retain the original per-pixel route. An exhaustive host comparison
covers all 256 two-pixel input bytes, all 16 previous tone pairs, all four
planes and offsets 0–11. Packing tests include partial bytes and guard bytes.
Existing recorded driver transitions, held-gray behavior, cleanup cadence,
resync, sleep and BUSY failure tests pass under ASan/UBSan.

The full dev21 build and 104-path SDK overlay check pass. The application is
6,406,768 bytes, with 146,832 bytes available in its slot, SHA-256
`86da503355829bd40f0a9a029b53ada67ca9183101ac648fe5f37d8936a85224`.
The ROM MAC, partition table and active app0 at `0x10000` were checked again.
The entire 6,553,600-byte dev20 slot was backed up and independently verified,
SHA-256 `fdb2e1d3fc8257fad5a8161a69cf9acb7928859ded3b7ead707805bf84d3ede8`.
Large USB reads stalled, so backup completed with 512-byte packets and
verified 64 KiB chunks. dev21 was written to the active app slot and passed
digest verification. Partition table, OTA metadata and NVS compare equal
before and after installation. SD mounted and the dev21 home was observed.

Automatic dev21 menu changes report retained gray transactions of
1,070–1,085 ms. The settings page reported 1,079 ms versus the earlier dev20
settings sample of 1,355 ms. Different source pages produce different changed
pixel counts, so this is a sampled transaction comparison. The residual wait
remains substantial. Repeated reading with the same EPUB and visual/input
acceptance are pending user verification.

The next user-operated session captured 25 ordinary turns after the cold
entry, excluding a menu-return repaint and a periodic clean. Total times
ranged from 991 to 1,679 ms, median 1,177 ms. B/W painting had a 576 ms median,
versus the earlier 809 ms sample, while gray display remained about 407 ms.
The user turned more pages in this session, so the two page sets and glyph
working sets are different. This is not a controlled percentage speedup.
One periodic clean took 2,095 ms in gray display and 3,020 ms for the page.
Subsequent menu transactions stayed around 1,076–1,080 ms. The user
subsequently reported substantially improved responsiveness.
Detailed optical checks remain separate from that feedback.

### 2026-11-07 dev22 ordinary reading motion

The ordinary control center removes its Fast Refresh tile and loads the
legacy `readerFastRefresh` value as zero. The saved field remains part of
the shared settings format. The ordinary display settings expose the
existing Off, Skim, Arc and Ripple choices, preserving saved style IDs.

Previously `HalDisplay::setNextPageTurn()` did nothing on ordinary E-Ink4,
and the app invoked it only inside the native 16-level render path. Ordinary
text uses the combined SSD1677 path instead. That path now submits a one-shot
request for an adjacent manual text turn with a committed predecessor and
matching orientation. Rapid queued turns use the existing motion-skip rule.
First entry, periodic/manual cleaning, image pages, panel fills and night mode
retain their corrective paths. With motion selected and AA disabled, text
still stages empty gray planes so the binary target can use the same motion
submission without introducing gray into the text.

SSD1677 motion partitions the target into two disjoint spatial groups. Each
group receives the complete accepted text waveform once. This adds a second
activation to an animated turn. It is a two-step spatial effect, distinct from
the finer staggered scans of the parallel Plus/Pico panels. Geometry comes
from the LGPL-licensed RickyOS Plus `ricky_native_turn.h`, including logical
orientation, handedness, shallow arc and touch-origin ripple. A 16×16-pixel
grid holds one group bit per cell, 188 bytes total. The driver's 2 KiB static
SPI staging block avoids the render stack. It does not retain the otherwise
unused 16 KiB rotation buffer on this target, and no extra framebuffer is
allocated for the effect.

Host traces cover all three styles, four orientations and both directions.
The groups' selectors are disjoint and their union equals the original whole
page selectors; each receives the original LUT. Cancellation after the first
group and BUSY failure in either group invalidate the baseline and require
cleanup. The unchanged standard submission and display-route suites pass.
The dedicated motion trace passes ASan/UBSan and checks that no additional
frame allocation occurs during motion. The SDK overlay now verifies 106 paths.

The final dev22 application is 6,410,400 bytes, with 143,200 bytes available in
the slot, SHA-256
`da8bbd68d0a57cf1c6c43db1342461f6f77afd3538f266edb0b8f51a95fd63e5`.
Its linked SPI staging symbol is 2,048 bytes. The initial dev22 build was
booted and resumed the EPUB successfully before this staging reduction.
Final installation and three-effect optical/latency acceptance are recorded
below when observed.

Final dev22 was written to the verified ordinary device's active app0 and
passed the app digest check. The partition table, OTA selection and current
NVS compare equal across the write. SD mounted and the home page appeared.
The 10-second boot sample reports 124,204 bytes free internal heap, an
81,908-byte largest block and 4,924,780 bytes free PSRAM. The total internal
heap is 240,292 bytes, versus 242,532 on dev21, consistent with the small
new motion state and 2 KiB staging buffer. The user has been asked to check
each of the three styles, the added delay, residual images and removal of
the Fast Refresh tile. Runtime motion logs report both `style` and `groups`.
Device interaction traces observed all three styles: Skim 778–779 ms in the
driver (3 turns), Arc 779–780 ms (3), Ripple 786–790 ms (6), versus 380–382 ms
for the ordinary one-group turn (4). One Ripple page took 1,543 ms overall.
The user reported sluggish turns and an unsmooth effect. This is failed visual
acceptance, not a completed animation port: each spatial group waits for its
own ~278 ms drive, leaving a visible two-step change. More groups would add
more complete activations. The current two selector bits encode idle, white,
gray and black; they do not also encode an independent spatial start time.
A single-activation staggered gray effect is not implemented or validated.

Following this feedback, ordinary fresh/missing/invalid settings now default
to motion off; the three styles remain explicit experimental options. Valid
saved choices are preserved, including the style selected during device tests.
Plus/Pico keep their existing default. This settings-only follow-up has not
been flashed: the installed image above remains the one used for the trace.
The device must select Off to compare the normal one-activation path. The
ordinary firmware build and Metalio settings translation check passed after
this default change (`build-dev22-default-off.log`).

### 2026-11-07 dev23 ordinary text-page cache

The user continued to report sluggish, unnatural turns. The dev22 trace includes
ordinary single-group pages at 1,030 and 1,169 ms, with 484–565 ms in B/W text
rendering, and later Ripple turns still using two activations. These do not
establish that the user's last observation was exclusively motion-off.

The ordinary reader now uses the existing foreground/idle page-cache identity,
with forward/backward text pages prepared on the render task. Each slot owns
four physical 48,000-byte PSRAM planes (B/W, two gray masks, a restore stash):
384,000 bytes maximum across both slots, allocated lazily with the existing
headroom and contiguous-block checks. No framebuffer is allocated on the stack.
OOM releases the optional cache and falls back to foreground rendering. Images,
CSS panel fills, inverted reading and AA-off pages retain their established
paths. Chapter prefetch is not enabled on this target.

On a hit, the foreground copies the cached B/W page, redraws current chrome,
and copies both cached gray planes through the established combined submission.
The accepted waveform and periodic cleaning policy remain unchanged. This is
an in-memory cache; no on-card format or version changes. Real physical input
now cancels idle work before any handler waits for RenderLock; interrupted
quiet-window preparation can re-arm. The cancellation, lifetime, cache-key,
allocation-failure and upload-equivalence host checks cover the ordinary target.
Two stale host seams were updated for the current paragraph-spacing field and
system-font initialization.

The ordinary-only diagnostic command `CMD:READER_MOTION_OFF` cancels idle work,
sets and saves motion off under RenderLock, and logs the persistence result.
It allows an explicit one-activation performance comparison without resetting
the settings file. All three experimental style choices remain available.

The initial dev23 build is 6,416,064 bytes, SHA-256
`f13828c14285142e6619431561db1ffce0c56c5dd1f823136137d7a29db7e8ac`.
Its installation passed image and metadata checks, resumed the EPUB, and
prepared forward/backward slots in 1,011/808 ms. Internal heap was 88,792 bytes
free; PSRAM had 3,909,752 bytes free with both slots allocated. The diagnostic
confirmed `READER_MOTION_OFF saved=1`.

Final dev23 also disables the forward-build touch-tolerant mode on ordinary
E-Ink4: touch cancels preparation at the next element boundary instead of
waiting for the whole prepared page. Plus/Pico retain their existing policy.
The final image is 6,416,016 bytes with 137,584 bytes of app-slot headroom,
SHA-256 `4024afd6fa7746d24a3ebdb9bf5b0e313db5fa44b2230bd3ad251cf32de3f34a`.
The five page-cache host tests, full-plane ASan/UBSan comparison, SDK overlay
check and final firmware build passed. Final dev23 installation passed the
app digest check; partition table, OTA metadata and current NVS remained equal.
The device booted to InxRecent. Its 10-second sample had 124,204 bytes free
internal heap (81,908-byte largest block), with 4,924,708 bytes free PSRAM.
The final dev23 interaction trace has one ordinary cache hit at 551 ms and
18 ordinary cache misses at 762–1,373 ms (median 1,129 ms), excluding entry/
corrective frames. Settings and page contents changed during this sample;
these are observed categories, not a controlled percentage comparison. The
user confirmed faster normal turns, with continuous turns and menus still
sluggish. The cached turn retained 130 ms in B/W/chrome rendering and 406 ms
in gray display. The first reader-menu entry took about 2.66 s from activity
entry to the end of retained-gray submission; the submission itself was
1,798 ms, including a corrective handoff. Text-settings submissions remained
1,064–1,073 ms, separate from paint time.

### 2026-11-07 dev24 font retention and idle warm-up

Ordinary E-Ink4 now keeps built-in decompressed hot groups across page resets,
using `clearPageCache()` as Plus already does. `releaseSdFontCaches()` still
frees the groups under pressure. The ordinary group LRU has 16 slots instead
of 8 (roughly 256 KB for 16 KB groups, plus lookup metadata; actual capacities
follow the built-in groups). This is deliberately smaller than the Plus's
160 slots. C3 and the other targets keep their prior limits/reset policy.

After optional next/previous-page preparation, ordinary E-Ink4 also uses the
existing cancellable TTF warm-up in batches of eight newly drawn characters.
The ordinary reader keeps its existing 1 MiB / 4,096-glyph face budget, rather
than the Plus's 4 MiB / 8,192 budget. The existing warm-up stops near the arena
limit or below the 2 MiB contiguous-PSRAM guard instead of evicting other work.
The list is generated from the first 4,000 entries of the shared frequency
order by `scripts/generate_reader_common_chars.py`; the ordinary build hook
creates the ignored header on a fresh checkout and avoids rewriting stable
output. Plus continues to use its port-owned list.

No driver waveform or cleanup cadence changes in dev24. The build succeeds,
as does a repeat build through the generator hook. Host tests exercise page
reset versus pressure eviction, actual decompression and 8/16-slot LRU
behavior under ASan/UBSan, and the five page-cache regressions. The app is
6,428,544 bytes with 125,056 bytes free in the slot, SHA-256
`3942cd5cb4bd9b3476bf3515cc2c6311a1770f2bd47d49b61af7bc2b6a94b5ad`.
dev24 installation passed the app digest check and preserved partition table,
OTA metadata and current NVS. Boot reached InxRecent; the 10-second sample
reported 123,876 bytes free internal heap (81,908-byte largest block), total
239,964 bytes, and 4,797,320 bytes free PSRAM. The smaller total internal heap
versus dev23 matches the additional fixed hot-group slot headers.

An automated settings/apps/settings/home comparison completed and returned
home. Host command-to-completion times were 1,866 / 1,582 / 1,362 / 1,757 ms;
retained-gray transactions themselves were 1,078 / 1,082 / 1,080 / 1,067 ms.
These are cold/warm UI observations, not a controlled attribution of all
saved time to font retention. The unchanged two-pass UI submission remains
about one second; this version does not solve that display-side latency.
The user has been asked to leave the same EPUB open for about 20 seconds,
then compare continuous turns and reader-menu entry. The user subsequently confirmed that continuous turns and menus both improved.
A cached ordinary text turn completed in 438 ms (18 ms B/W/chrome, 4 ms gray
render, 6 ms gray write, 407 ms gray display). Twelve uncached ordinary turns were 684–826 ms (median 771 ms), with
120–204 ms in B/W/chrome rendering. The waveform stayed on one activation
with motion off. These observations support retaining dev24 as the
current performance baseline. They do not establish smooth animation support:
the three experimental two-stage styles remain opt-in and motion is off on
the tested device. The final settled reader sample had 118,952 bytes free
internal heap (73,716-byte largest block) and 2,662,944 bytes free PSRAM; the
observed minimum free PSRAM was 2,067,452 bytes. The 1,834,996-byte largest PSRAM
block was below the warm-up growth guard; the exact warmed-glyph count was
not logged. No runtime crash was logged.

Logs, baseline timing JSON, app image, installation verification and the dev20
slot backup are local in `artifacts/metalio-dev20-latency-20261106/` beneath
the enclosing workspace, two directories above this worktree. These device
artifacts are not repository inputs.

### 2026-11-10 UI single-activation candidate

Normal retained-UI changes now submit target white, changed gray and target
black in one activation, using the accepted Metalio text-turn timing (32 white
preparation + 24 weak-gray frames; 32 frames at each endpoint). Unchanged
pixels, including held middle tones, remain idle. The four-tone raster and its
committed 2bpp optical history are unchanged. Initial paints, forced/periodic
cleanup, non-UI handoffs, sleep/wake and failed activations retain the existing
safe cleaning route; every sixth changed UI frame still cleans fully.

The recording-bus checks cover all 16 old/new tone combinations, aligned and
unaligned selector conversion, the complete 56-frame LUT, analog tail,
identical-frame suppression, cleanup cadence, resync, sleep and BUSY failure.
The existing SSD1677 driver, text-routing and independent text-turn waveform
checks also pass. The `rickyos_metalio_eink4` firmware builds successfully.
Visual quality, accumulated residue and device latency require a separate
on-panel comparison; software traces do not establish optical acceptance.

The 6,429,056-byte application has SHA-256
`19262fa667b91273ce53275c8bb5824f707edd6bd6e65fd5ce4738032dea3ac3`.
It was written to `/dev/cu.usbmodem1101` (ESP32-S3 revision 0.2, 16 MiB flash,
MAC `10:20:ba:6d:6c:cc`); the uploader's written-data hash passed before a hard
reset. A subsequent serial sample at 20 seconds reported 124,068 bytes free
internal heap, an 81,908-byte largest block and 5,109,920 bytes free PSRAM, with
no captured panic. This confirms installation and a running main loop, not the
single-activation panel's optical quality. The 106-path SDK overlay manifest and
patch match the tested source snapshot.

**The initial candidate failed physical acceptance.** The user's panel
photograph after menu changes showed severe superposition of multiple previous
pages. Root-cause review found an endpoint-polarity bug in that candidate: the
quantized UI raster is 0=black and 3=white, but its first combined selectors
assigned those endpoints in the opposite direction. That error invalidated the
optical test; it is not evidence that the text-turn timing itself cannot work.
The corrected candidate makes the polarity explicit and tests all 16 source/
target combinations against that contract. The rejected image and prior
two-stage path remain available independently of this corrected candidate.
Its application SHA-256 is
`65b0a4638904c74e3b0f31c65c83aeb8f237a2134c37d85ba8a37e6a86297791`.
Installation and renewed optical acceptance are recorded separately; the host
checks and successful build do not undo the initial on-panel failure.

After exclusive access was restored, `chip-id` again reported ESP32-S3 revision
0.2 and MAC `10:20:ba:6d:6c:cc`. The corrected application was written to
`/dev/cu.usbmodem1101` and passed the uploader's hash check. Six driven UI
transitions (`settings`, home, library, apps, settings, home) logged five combined
updates at 605–613 ms followed by the expected sixth-frame clean at 1,796 ms.
No refresh timeout or panic was captured. The user then confirmed that the
corrected panel output was normal, while finding gray text and rules too light.
This accepts the corrected single-activation route at a basic visual level; it
does not yet establish long-term residue or endurance.

For the follow-up tone comparison, the native-small `ink-2` and `line` tokens move
from level 5 to level 8, selecting the darker of the panel's two physical middle
tones after four-tone quantization. `mute=10`, `tint=15`, the waveform and cleanup
cadence are unchanged. The locked-token host check passes. Installation and the
user's comparison of this darker tone are separate from the waveform acceptance.

The darker-tone application SHA-256 is
`4917cd03a1046f8ce4f1955e17508d52fdcfe68dcea6c3a73651be941b6fb273`.
It passed the device write/hash check. A settings/home transition pair then logged
combined updates at 608 and 602 ms. The user's tone comparison remains pending.
Several unrelated pre-existing UI source tests still reference older interfaces
and layouts; they were not changed to disguise those baseline failures.

The user still found the level-8 middle tone light. The next candidate therefore
keeps the palette and selectors unchanged but increases only the UI combined
waveform's weak-rail development from 24 to 32 frames. This extends that phase by
40 ms at 5 ms/frame and leaves the text reader's 24-frame calibration untouched.
The frame-by-frame host decoder verifies 64 total frames, 32 weak-gray frames,
unchanged endpoint timing, all 16 transitions and the existing failure/cleanup
paths. Device installation and optical comparison are recorded after they occur.

The 32-frame application SHA-256 is
`2ba837e80d6cb92901ae467c9ffdad7109018c721812d8c2a86b910ac0b9dbdb`.
It passed the write/hash check; settings/home then logged 648/642 ms combined
transactions with no captured timeout or panic. The user's middle-tone comparison
is pending.

A subsequent panel photograph still showed secondary labels, reading metadata and
inactive tabs too faint for comfortable reading. The next candidate retains the
same 64-frame activation but changes its final eight gray-development frames from
the weak rail to the strong-black rail (24 weak + 8 strong, rather than 32 weak).
The endpoint, reader and cleanup paths are unchanged. Host decoding verifies the
strong/weak boundary and all existing UI transitions and recovery behavior.
Installation and optical acceptance remain separate.

The 24-weak + 8-strong application SHA-256 is
`469d3ff2912b09bd040cd375eac9610c338df0c7e4c8abdaf75619a2ca2e856e`.
It passed the write/hash check; settings/home logged 648/643 ms combined
transactions, with no captured timeout or panic. The user's visual comparison is
pending.

The user found eight strong frames too dark, with middle tones approaching black.
The following candidate uses 30 weak + 2 strong frames within the same 64-frame
activation. The host waveform decoder checks the moved boundary and exact frame
counts; selectors, endpoints and cleanup remain unchanged. Installation and visual
comparison are recorded separately.

The user also observed option borders alternating between gray and nearly black in
Settings and Apps. Normal combined frames and initial/periodic cleaning were using
different gray LUTs. The cleaning path now supplies the same combined LUT to its
gray-only pass, so its middle tones follow the current calibration instead of the
legacy table. Host checks decode the cleaning frame's LUT as well as the normal
combined frame. This addresses that route inconsistency; the intentional B/W
fallback in `RickyGrayStroke` remains unchanged for non-gray/error frames.

The combined 30-weak + 2-strong plus consistent-clean application has SHA-256
`33cc7e61087879aa3ef807174df735bb26582bb8ba15d2c90a7d90709ad37bcb`.
It passed the write/hash check. Six Settings/Apps alternations logged five combined
updates at 648–650 ms and the sixth-frame clean at 2,068 ms, without a captured
timeout or panic. Border-tone and middle-tone visual acceptance remain pending.

### UI change-scan profiling and batching

The 30-weak + 2-strong consistent-clean baseline was then instrumented without
changing its waveform. Three Settings/Apps transitions measured scan/upload/drive/
restore/total at 88–90 / 186 / 400 / 62 / 738–739 ms. The previous total started
after the change scan, so it excluded that work.

`MetalioUiGray::countChanges()` now compares four committed pixels per history byte,
using the existing quantized-pair table plus a 256-entry pair-difference table.
No heap allocation or output-format change is introduced. Host checks compare the
batched result with per-pixel counting over the selector fixture, and the existing
UI/routing suites pass. On the same three device transitions, scan/upload/drive/
restore/total measured 19–20 / 184–185 / 399–400 / 61–62 / 666–668 ms. This is an
observed same-device comparison, not a claim about other panels. The application
SHA-256 is `4d3d45dd7c8531370e63df488860c44d1d1480250aae65a60d8f2f80d5ef00a7`;
its write/hash check passed with no captured refresh timeout or panic.

### Wallpaper dither strength

The custom wallpaper path uses Atkinson with original thresholds. Its six-neighbor
error distribution was 75%, producing visible grain in gradients. `Bitmap` and
`AtkinsonDitherer` now accept an optional error-shift value; the default remains 3
(75%), while the two custom-sleep wallpaper loads use 4 (37.5%). Book covers,
readers and other image paths retain their original strength. A small host check
verifies the reduced propagation and unchanged four levels, and the full firmware
build passes. Visual comparison of the wallpaper remains separate from these
algorithmic checks.

The reduced-dither application SHA-256 is
`5188e04e70c28b823918499c2cfb92a0b2d7b51ea96c240f2c83073b8c0f22b8`.
It passed the write/hash check. The existing combined UI refresh also completed
normally after installation. The user's comparison of grain and gradient banding
is pending.

### 2026-11-14 dev25 network controls and single photo

The ordinary board now exposes separate Wi-Fi and 4G tiles in Control Center.
Tapping changes the corresponding switch. Holding opens the existing Wi-Fi
picker or the mobile-data settings page. The latter also appears in Settings
under connectivity and lists both SIM slots before the modem diagnostics.
A SIM change uses a temporary flight-mode AT session and verifies the selected
slot after the modem restarts. The mobile-data preference and verified slot
remain in the existing `rickcell` NVS namespace.

Once Wi-Fi station obtains an IP, ESP-NETIF prefers it over the cellular
Ethernet interface. The main loop suspends the modem while Wi-Fi is connected.
Loss of Wi-Fi restarts the modem when mobile data remains enabled. The Inx
status bar and reader header report the active default interface. The
Control Center Wi-Fi switch reconnects to the most recently joined network
using its saved credentials. A Wi-Fi connection made through Settings or
Control Center is shared by app sessions for the current boot.

The camera shutter saves a usable displayed preview frame or runs an
independent single capture if the viewfinder cannot supply one. Earlier board
logs verified a 320×240 JPEG saved to `/images/camera`. They also recorded an
enumeration timeout during a separate viewfinder test. The firmware build,
SDK overlay, control-center gesture, Wi-Fi startup, Metalio host hardware
and settings translation checks passed. USB detection, live route switching,
both SIM slots and photo capture on dev25 still require a connected device.

### dev26 camera and cellular icons

Mobile-data configuration stays under Settings → Network & sync, with a tap
to toggle and a long press to configure from Control Center. The Apps catalog
drops the duplicate 4G entry and uses the Lucide camera for the camera app.
Control Center uses the Lucide signal bars for 4G. Both icons have 1-bit and
16-level assets generated from the same source as the other RickyOS icons.

### dev27 image dithering

The image viewer and unfiltered custom standby and power-off wallpapers now
quantize decoded luminance to the four calibrated SSD1677 tones with a 32×32
blue-noise threshold tile. Dithering is applied at display-pixel coordinates
after resampling. A narrow range around each panel tone is rendered flat to
suppress visible specks. The four-tone result uses the existing retained
grayscale path. The source image and cache formats are unchanged.

A host comparison rendered the same gradient and grayscale photo with
reduced-strength Atkinson and blue noise. Blue noise dispersed the diagonal
patterns visible in the Atkinson frame. Host checks covered balanced mask
ranks, unchanged physical tones and monotonic tile-average luminance. These
checks cover raster preparation. Optical appearance still needs a panel
comparison.

### dev28 photo waveform and shadow lift

The user reported that dev27 images looked too dark. The retained UI waveform
drives both middle tones through the same LUT entry, so it can collapse photo
tones even while the image raster contains all four codes. The image viewer
and unfiltered custom wallpapers now use the original SSD1677 four-tone
overlay with separate middle-tone selections. The retained UI path stays in
use for interactive pages.

The photo quantizer narrows the flat range around each gray and lifts shadows
and midtones while keeping black and white fixed. Host tests check the
monotonic tone response and the four physical output codes. The same source
image was rendered with both quantizers for a digital comparison.

### dev29 home and library cover tones

The ordinary board now quantizes the existing gray thumbnails against the
three optical tones of the retained UI waveform. Blue-noise mixing between
adjacent optical tones preserves shadow and highlight distinctions that would
otherwise collapse around the single midtone. The pattern is anchored to the
cover after scaling. The crop and fit rules, stored thumbnails, panel tile
cache and UI refresh cadence are unchanged.

### 1.2.24 development candidate

Ported the Plus fixes for redundant reader repaints when switching input modes
and locked WeRead font measurement in the disclaimer and introduction. Dictionary
management lists installed SD dictionaries before starting Wi-Fi, keeps the
selected dictionary on save failure, and returns to the local list when online
selection is cancelled. The online catalog still uses the existing validation and
installation flow.

To meet the OTA slot budget, the ordinary release build shares its native bitmap
fallback between body text and book titles. System UI fonts still supply the default
book-title outline. The independent offline reading face and its pagination metrics
remain unchanged. OTA uses a board-specific manifest, rejects other board images and
keeps the existing 512 KiB slot reserve. See [OTA policy](rickyos-firmware-update.md).
The build is a test candidate pending on-device acceptance.
