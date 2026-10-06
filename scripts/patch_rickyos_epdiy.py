"""Let a 16-level page commit use the text-turn waveform, only for RickyOS.

The pinned SDK commits every native 16-level frame with the Half profile: GL16 on
the default table, which also drives pixels that did not change, so anti-aliased
page turns flash. The driver already carries the held-diagonal text-turn table
for exactly this; this adds a one-shot switch the reader sets before an ordinary
page turn, keeping the panel rails up between turns as the other page paths do.

Like patch_rickyos_tls.py: a bounded mechanical rewrite, re-runnable, and the build
fails if the SDK source no longer matches instead of silently skipping it.
"""
from pathlib import Path

MARK = 'RICKYOS_GRAY16_TEXT_TURN'

OLD_COMMIT = '''bool epdiyLcdCommitGrayscale16(const uint8_t* bwProxy) {
  if (!g_nativeGrayActive || !epdiyLcdReady() || bwProxy == nullptr) return false;
  g_nativeGrayActive = false;
  if (!pushFrame(EpdiyLcdRefresh::Half, true)) return false;
'''
NEW_COMMIT = '''// RICKYOS_GRAY16_TEXT_TURN: one-shot profile for the next 16-level commit
// (0 = Half as before, 1 = held-diagonal text turn, 2 = Full GC16 cleanup,
// 3 = Full GC16 for a picture that stays up: rails off afterwards).
static uint8_t g_gray16Profile = 0;
void epdiyLcdSetGray16Profile(uint8_t profile) { g_gray16Profile = profile; }

bool epdiyLcdCommitGrayscale16(const uint8_t* bwProxy) {
  if (!g_nativeGrayActive || !epdiyLcdReady() || bwProxy == nullptr) return false;
  g_nativeGrayActive = false;
  const EpdiyLcdRefresh grayMode = g_gray16Profile == 1   ? EpdiyLcdRefresh::TextTurn
                                   : g_gray16Profile >= 2 ? EpdiyLcdRefresh::Full
                                                          : EpdiyLcdRefresh::Half;
  // Reader page turns keep the rails up like every other page path: powering off costs a
  // fixed 500 ms PMIC hold plus a power-up on the next page. Long-lived frames (standby
  // picture, image viewer) still power down.
  const bool turnOff = g_gray16Profile == 0 || g_gray16Profile == 3;
  g_gray16Profile = 0;
  if (!pushFrame(grayMode, turnOff)) return false;
'''
OLD_DECL = 'bool epdiyLcdCommitGrayscale16(const uint8_t* bwProxy);\n'
NEW_DECL = ('bool epdiyLcdCommitGrayscale16(const uint8_t* bwProxy);\n'
            '/// RICKYOS_GRAY16_TEXT_TURN: profile for the next 16-level commit\n'
            '/// (0 = Half, 1 = text turn, 2 = Full, 3 = Full, rails off).\n'
            'void epdiyLcdSetGray16Profile(uint8_t profile);\n')

# RICKYOS_RAILS_IDLE: text turns and fast frames leave the panel rails up so the next
# frame starts at once, but the rails then stayed up for a whole reading session. With
# high voltage held and nothing driven, charge builds in the film and old pages show
# through new ones (two screens superimposed). The driver now records when rails were
# left up, and the caller drops them after an idle interval.
RAILS_MARK = 'RICKYOS_RAILS_IDLE'
OLD_POWER = """  if (turnOff || !g_baselineKnown) epd_poweroff();
  return g_baselineKnown;
}
"""
NEW_POWER = """  // RICKYOS_RAILS_IDLE: remember rails left up and when, for epdiyLcdRailsOffIfIdle().
  if (turnOff || !g_baselineKnown) {
    epd_poweroff();
    g_railsUp = false;
  } else {
    g_railsUp = true;
  }
  g_lastPushUs = esp_timer_get_time();
  return g_baselineKnown;
}
"""
OLD_POWER_STATE = 'bool g_baselineKnown = false;\n'
NEW_POWER_STATE = ('bool g_baselineKnown = false;\n'
                   'bool g_railsUp = false;       // RICKYOS_RAILS_IDLE\n'
                   'int64_t g_lastPushUs = 0;     // RICKYOS_RAILS_IDLE\n')
OLD_IDLE_FN = 'bool epdiyLcdReady() {'
NEW_IDLE_FN = """// RICKYOS_RAILS_IDLE: drop rails left up by the last frame once no frame has followed
// for idleMs. The caller serializes against frame pushes (render lock).
void epdiyLcdRailsOffIfIdle(uint32_t idleMs) {
  if (!g_railsUp || esp_timer_get_time() - g_lastPushUs < static_cast<int64_t>(idleMs) * 1000) return;
  epd_poweroff();
  g_railsUp = false;
}

bool epdiyLcdReady() {"""
OLD_TIMER_INCLUDE = '#include <esp_heap_caps.h>\n'
NEW_TIMER_INCLUDE = '#include <esp_heap_caps.h>\n#include <esp_timer.h>  // RICKYOS_RAILS_IDLE\n'
OLD_IDLE_DECL = 'bool epdiyLcdCommitGrayscale16(const uint8_t* bwProxy);\n'
NEW_IDLE_DECL = ('bool epdiyLcdCommitGrayscale16(const uint8_t* bwProxy);\n'
                 '/// RICKYOS_RAILS_IDLE: power the panel rails down after idleMs without a frame.\n'
                 'void epdiyLcdRailsOffIfIdle(uint32_t idleMs);\n')


def patch_text(source, old, new, mark=MARK):
    if new in source:
        return source
    if source.count(old) != 1:
        raise RuntimeError('SDK epdiy source changed; review the RickyOS patch (%s) before building.' % mark)
    return source.replace(old, new)


def apply(project_dir):
    base = Path(project_dir) / 'freeink-sdk/libs/display/EpdiyLcd'
    edits = {
        base / 'src/EpdiyLcd.cpp': ((OLD_COMMIT, NEW_COMMIT, MARK), (OLD_TIMER_INCLUDE, NEW_TIMER_INCLUDE, RAILS_MARK),
                                   (OLD_POWER, NEW_POWER, RAILS_MARK),
                                   (OLD_POWER_STATE, NEW_POWER_STATE, RAILS_MARK),
                                   (OLD_IDLE_FN, NEW_IDLE_FN, RAILS_MARK)),
        base / 'include/EpdiyLcd.h': ((OLD_DECL, NEW_DECL, MARK), (OLD_IDLE_DECL, NEW_IDLE_DECL, RAILS_MARK)),
    }
    for path, steps in edits.items():
        source = path.read_text()
        patched = source
        for old, new, mark in steps:
            patched = patch_text(patched, old, new, mark)
        if patched != source:
            path.write_text(patched)


if 'Import' in globals():
    Import('env')
    if env.subst('$PIOENV') in ('rickyos_readpico', 'simulator_rickyos'):
        apply(env.subst('$PROJECT_DIR'))
elif __name__ == '__main__':
    apply(Path(__file__).resolve().parents[1])
