# Architecture & Common Patterns

> Deep reference for [AGENTS.md](../../AGENTS.md). For the big-picture runtime
> lifecycle, activity model, and dataflow diagrams, see
> [../contributing/architecture.md](../contributing/architecture.md). This file
> covers the agent-facing details: directory layout, the HAL, and the reusable
> code patterns (singletons, activity lifecycle, FreeRTOS tasks, fonts).

## Directory Structure
* lib/: Internal libraries (Epub engine, GfxRenderer, UITheme, I18n)
  * lib/hal/: Hardware Abstraction Layer (HalDisplay, HalGPIO, HalStorage)
  * lib/I18n/: Internationalization (translations in `translations/*.yaml`, generated string tables)
* src/activities/: UI logic using the Activity Lifecycle (onEnter, loop, onExit)
* freeink-sdk/: Low-level SDK (FreeInkDisplay, InputManager, BatteryMonitor, SDCardManager)
* .crosspoint/: SD-based binary cache for EPUB metadata and pre-rendered layout sections

## Hardware Abstraction Layer (HAL)

**CRITICAL**: Always use HAL classes, NOT SDK classes directly.

| HAL Class | Wraps SDK Class | Purpose | Singleton Macro |
|-----------|----------------|---------|-----------------|
| `HalDisplay` | `EInkDisplay` | E-ink display control | *(none)* |
| `HalGPIO` | `InputManager` | Button input handling | *(none)* |
| `HalStorage` | `SDCardManager` | SD card file I/O | `Storage` |

**Location**: [lib/hal/](../../lib/hal/)

**Why HAL?**
- Provides consistent error logging per module
- Abstracts SDK implementation details
- Centralizes resource management

**Example - HalStorage**:
```cpp
#include <HalStorage.h>

// Use Storage singleton (defined via macro)
HalFile file;
if (Storage.openFileForRead("MODULE", "/path/to/file.bin", file)) {
  // Read from file
  // No file.close() needed — DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}
```

**Usage**: Use `HalFile` (the mutex-wrapping handle), NOT raw SdFat `FsFile` or Arduino `File`. Do NOT add `file.close()` for local variables (see [build-system.md](build-system.md) → DESTRUCTOR_CLOSES_FILE).

**SdFat is not thread-safe; all SD access MUST go through HalStorage**:
- SdFat's `SdSpiCard` tracks SPI bus state with an unsynchronized `m_spiActive` bool. Two tasks calling SdFat concurrently can confuse that state machine and end with one task calling `SPIClass::endTransaction()` against a paramLock the *other* task is holding. That trips FreeRTOS's `xTaskPriorityDisinherit` assert (`tasks.c:5156, pxTCB == pxCurrentTCBs[0]`) and panics the system. See SdFat issue #518.
- `HalStorage` serializes everything via `storageMutex`. Downstream code uses `HalFile` (declared in `<HalStorage.h>`); every method call (read, write, seek, close) takes the mutex. `HalFile`'s destructor also takes the mutex before letting the underlying SdFat `FsFile` close.
- **Never** call into `SdFat` / `SdSpiCard` / `FsBaseFile` / `SDCardManager` / raw `FsFile` directly — that bypasses the mutex.

**CPU frequency changes must go through `HalPowerManager::setPowerSaving()`**:
- `modeMutex` protects the target-mode decision, the entire `setCpuFrequencyMhz()` call, and the `isLowPower` update. Update state only after a successful transition; repeated requests for the current state do nothing.
- Arduino's APB callbacks hold SPI locks between the BEFORE and AFTER callbacks. Concurrent frequency changes can deadlock: one task holds a SPI lock while waiting for the APB callback lock, and another holds the APB callback lock while waiting for that SPI lock. Serializing only the state assignment is insufficient.
- `HalPowerManager::Lock` sets `NormalSpeed` under `modeMutex`, then releases the mutex **before** calling `setPowerSaving(false)`. Keep that ordering: the mutex is not recursive. Obtain the power lock before starting SPI work. The existing helper supports only one active power lock; it is not a nested-lock counter.
- Preserve the Wi-Fi/C3 BLE host guards and board-specific frequency restrictions. Light sleep and CPU downclocking are separate decisions; fixing clock serialization must not disable either Bluetooth or standby light sleep.

`Storage.getSpace(totalBytes, freeBytes)` queries filesystem data capacity, not
raw card size, through the SDK's unified capacity query under the same mutex.
Both outputs are zero on failure; callers must check the boolean result before
using them. The SDK derives cluster bytes from 512-byte sectors with 64-bit
arithmetic because SdFat 2.3.1's FAT `bytesPerCluster()` truncates 64 KiB to zero.
Successful free-space scans are cached for 20 seconds from completion; failures
invalidate the cache and are retried on the next query. Mount/unmount resets it.
The cache is a snapshot, not an immediate accounting of subsequent writes.
The About page paints its loading state before this synchronous query; input
waits for the scan to finish. Host simulator capacity comes from `statvfs`, so
use `python3 scripts/tests/test_sd_space.py` for FAT calculation regressions.

### Plus standby storage retention

An SD card that needed a retry after waking remains mounted in later standbys.
This disables SD parking, not CPU sleep: under the same HAL storage lease, the
board falls back to its existing shallow sleep in at most 60-second slices,
without changing live SD pads, rails or file positions. RTC cleanup failure still
blocks the fallback. Plus standby also releases inherited idle Wi-Fi and lets a
painted missing-image prompt sleep. See the
[candidate and hardware verification procedure](../../ports/metalio-e-ink4-plus/docs/plus-standby-power-20261102.md).

Opt-in `RICKYOS_PLUS_POWER_CAPTURE=1` records two fixed-size battery-only charge
windows in 108 bytes of static RAM. HAL samples through the existing awake poll
and standby boundaries; it adds no sleep wakeups or SD/NVS logging. Frozen
endpoints survive USB reconnect, with register failures, charge/reset/jump and
incomplete standby intervals rejected. CPU sleep time and gauge-indicated charge
are separate evidence; low-current deadbands and calibration still limit the
result. See the [measurement procedure](../../ports/metalio-e-ink4-plus/docs/plus-power-capture-20261102.md).

---

## Common Patterns

### Singleton Access
**Available Singletons**:
```cpp
#define SETTINGS CrossPointSettings::getInstance()  // User settings
#define APP_STATE CrossPointState::getInstance()    // Runtime state
#define GUI UITheme::getInstance()                   // Current theme
#define Storage HalStorage::getInstance()            // SD card I/O
#define I18N I18n::getInstance()                     // Internationalization
```

### Activity Lifecycle and Memory Management

**Source**: [src/main.cpp:132-143](../../src/main.cpp)

**CRITICAL**: Activities are **heap-allocated** and **deleted on exit**.

```cpp
// main.cpp navigation pattern
void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;  // Activity deleted here!
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  currentActivity = activity;  // Heap-allocated activity
  currentActivity->onEnter();
}
```

**Memory Implications**:
- Activity navigation = `delete` old activity + `new` create next activity
- Any memory allocated in `onEnter()` MUST be freed in `onExit()`
- FreeRTOS tasks MUST be deleted in `onExit()` before activity destruction
- Member `FsFile` handles MUST be closed in `onExit()` (local `FsFile` variables auto-close via destructor)
- `ActivityManager` calls `onExit()` while already holding `RenderLock`; never acquire a second render lock there

**Activity Pattern**:
```cpp
void onEnter()  { Activity::onEnter(); /* alloc: buffer, tasks */ render(); }
void loop()     { /* read the mappedInput snapshot; handle input */ }
void onExit()   { /* free: vTaskDelete, free buffer, close member FsFiles */ Activity::onExit(); }
```

The main loop updates mapped input once per frame before
`ActivityManager::loop()`. Normal Activities must not update it again.

**Critical**: Free resources in reverse order. Delete tasks BEFORE activity destruction.

### Read Pico idle page cache

The EPUB reader prepares the next text page on the existing render task, once
400 ms have elapsed after a foreground render without another render notification.
Other activities return zero from `idleRenderDelayMs()` and add no idle wakeups.
Input, foreground render requests and activity switches cancel the idle pass;
its timeout must not acknowledge a `requestUpdateAndWait()` caller.

The cache uses four reusable PSRAM frames (415,872 bytes on Read Pico), released
in `onExit()`. A scoped restore protects the live framebuffer and render mode.
Glyphs load on demand, with cancellation between page elements and planes;
a single SD read or glyph render is not preemptible. Cache hits compare the
resolved layout, drawing settings, chapter generation and render epoch. Image
pages, failed loads and cancelled attempts do not retry until another foreground
render. Allocation failure disables the cache for the reading session.

Run `python3 -m unittest discover -s scripts/tests -p test_reader_page_cache.py -v`
for cache identity, cancellation, allocation cleanup and render-task scheduling.
Device acceptance must still check short taps/swipes/menu entry during cache
construction, reflow to the old cached page number, image-page idle SD activity,
framebuffer integrity and PSRAM recovery after leaving/reopening the reader.

### Plus native reading cache

Plus native page caching accepts anti-aliased text and binary text with a valid
native page-turn style. The AA setting still controls glyph coverage; it must not
silently disable the binary animation's idle pre-render. Both use the same bounded
two-slot pool, existing PSRAM headroom/cancellation checks and reader-exit cleanup.
An AA change invalidates the pixel key. Binary reading without a native turn keeps
the previous cache gate. Test both settings in `test_plus_reader_page_cache.py`;
check partially allocated slots remain owned and are released on exit.

### Plus reader exit cleanup (local ui3 experiment)

Before releasing reader-owned images, page frames or overlay snapshots, finish
the pending display transfer while holding the existing render lock. Do not take
a second render lock in `onExit()`. Plus also sheds rebuildable font caches and
re-arms TTF warm-up; shared font sources and UI missing-glyph fallback faces stay
valid. Activity destruction releases the book and section before the destination
enters. Request the destination's full refresh after any exit notifications; the
direct-gray controller consumes it only after a successful commit and suppresses
the Tab animation for that clean frame. This is separate from normal page turns.

Run `ports/metalio-e-ink4-plus/tests/test_plus_reader_exit.py` and the existing
direct-gray/page-cache regressions. Real residual ink and post-reading heap
recovery still require device acceptance; see the Plus investigation dated
2026-10-26.

### FreeRTOS Task Guidelines

**Source**: [src/activities/util/KeyboardEntryActivity.cpp:45-50](../../src/activities/util/KeyboardEntryActivity.cpp)

**Pattern**: See Activity Lifecycle above. `xTaskCreate(&taskTrampoline, "Name", stackSize, this, 1, &handle)`

**Stack Sizing** (in BYTES, not words):
- **2048**: Simple rendering (most activities)
- **4096**: Network, EPUB parsing
- Monitor: `uxTaskGetStackHighWaterMark()` if crashes

**Rules**: Always `vTaskDelete()` in `onExit()` before destruction. Use mutex if shared state.

### Global Font Loading

**Source**: [src/main.cpp:40-115](../../src/main.cpp)

**Built-in fonts are global static objects** at firmware startup:
- International UI fonts: 8, 10, 12pt
- Simplified-Chinese UI fallbacks: 8, 10, 12pt
- Offline reader fallback: 12pt; other sizes and styles use SD `.cpfont`

OpenDyslexic and the broad Noto reader families live in the SD font catalog
(`lib/EpdFont/scripts/sd-fonts.yaml`). Legacy IDs resolve to the 12pt fallback.

**Compilation Flag**:
```cpp
#ifndef OMIT_FONTS
  // Most fonts loaded here
#endif
```

**Implications**:
- Fonts stored in **Flash** (marked as `static const` in `lib/EpdFont/builtinFonts/`)
- Font rendering data cached in **DRAM** when first used
- `OMIT_FONTS` can reduce binary size for minimal builds
- Font IDs defined in [src/fontIds.h](../../src/fontIds.h)

**Usage**:
```cpp
#include "fontIds.h"

renderer.insertFont(FONT_UI_MEDIUM, ui12FontFamily);
renderer.drawText(FONT_UI_MEDIUM, x, y, "Hello", true);
```


### Plus experimental fine reader ripple

`RICKYOS_PLUS_READER_FINE_RIPPLE=1` is an opt-in S31-only launch schedule for
reader style3. It uses 24 radial positions (spread23); other reader styles, UI
regions, and targets retain their schedules. The 48-phase quality waveform thus
needs 71 global scans instead of 55: drive sequences are preserved, elapsed time
is not assumed unchanged. The renderer reserves 24 * 256 = 6144 internal bytes
for direct action lookup (default flag0 retains9 * 256 = 2304 bytes). This adds
3840 bytes of bounded static storage, with no fragmented per-turn allocation or
new framebuffer. The earlier 2-bit packed variant is rejected: an actual
1216x684 boot exercise underruns at row670/tick14 even after inlining helpers.
Do not reintroduce packed per-pixel decoding based on host tests alone.

Preparation precedes both producers. Row feeders read only the internal byte
window; tiny schedule selectors are forced inline to avoid Flash calls per run.
No LCD clock, queue depth, heap floor, waveform action or clean interval changes.
Opt-in `RICKYOS_PLUS_TURN_SELFTEST=1` runs at most16 full-panel transitions (eight matched OFF/ON patterns) before
application startup, stops on the first driver error and resynchronizes the
normal screen afterward. It has no SD/preferences access and uses existing
frames. Disable the boot exercise in the normal trial image; require on-device
result0 for every trial plus optical/latency acceptance and a total internal
code/data budget comparison before enabling by default.

The S31 fine-ripple hot ring tracks its last prepared tick in one context int.
Sequential ticks load only the newly entering source phase; tail ticks load none.
Begin/end reset the marker. Cold/repeated/jumped/rewound ticks seed the full active
range; both producers must finish before preparation, and ISR never reads this
ring. Other modes retain their original fill policy. Source waveform/drive counts
stay exact; measure actual cadence and optical ghosting rather than inferring
screen quality from reduced table reads.


Plus optical A/B flag `RICKYOS_PLUS_READER_MOTION_PERIOD_US` defaults to0.
With phase-lookahead it optionally schedules pure native-text motion at a fixed
12000..25000us scan-start interval (trial20000). Deadline is the preceding
actual scan start plus the interval; never catch up from an earlier ideal
schedule. Existing prefill/error checks precede the hardware deadline wait,
which stays outside critical sections and preserves CKV/prefix timing. The
final phase retains its full settling interval. OFF/UI/paper/normal cleanup
keep their paths. LUT counts/order do not change, but physical duration does;
ink/gray/ghosting/latency require optical A/B before release. No new heap pool
or framebuffer is allocated. Keep the confirmed native16 wake-cleanup fix.


The S31 fine ripple optionally accepts `RICKYOS_PLUS_READER_RIPPLE_SPREAD=9..23`
(default23). Trial15 retains the accepted20ms motion period and48 local phases,
reducing only spatial staggering from71 to63 global scans. Geometry/origin stays
radial smoothstep with16 launch positions. Keep the existing6144-byte maximum
hot ring;16 active slots use4096 bytes but unused bytes are not reclaimed heap.
Indexing/incremental reuse follow the configured spread, with UI precedence.
Verify all local actions/rotations/ring wraps/recovery and hardware cadence;
shorter stagger/post-completion HOLD and tighter animation still need optical
acceptance. The q7 package remains the rollback baseline.


### Plus q6-based ripple erase overlap experiment (2026-10-27)

`RICKYOS_PLUS_READER_ERASE_OVERLAP_PHASES` defaults to0, range0..8,
requires full reader quality and early erase. Only S31 white-text ripple applies
it after completion alignment and before starting either feeder. It delays only
white-target exterior HOLD to overlap departing-text erase with new-black write;
all active pulses and internal HOLD remain, black/gray/chrome unchanged. Trial8
uses spread15 and period0 (q6 cadence), not q7's rejected fixed20ms. No new
allocation or pool;6144B hot-table maximum remains reserved. Runtime timing,
short old/new text overlap and optical ghosting must be checked separately.
See the port's plus-display-memory-investigation-20261026.md for evidence.


Plus q10 extends the opt-in erase-overlap preparation to all three reading
motion styles (1..3), retaining motion OFF and UI paths. S31 SWEEP_SPREAD is
explicit3..7/default7; trial4 reduces skim/arc spatial launch staggering by3
scans with all48 per-pixel waveform steps retained. Hot-ring modulo follows
the actual configured spread, UI region takes precedence. Paper motion uses
the shorter spatial sweep but retains its unmodified waveform. Boot-only
selftest now compares OFF/skim/arc/ripple in32 bounded matched transitions;
no extra frame/heap/queue. Hardware optical acceptance remains separate.
