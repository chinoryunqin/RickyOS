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


def patch_text(source, old, new):
    if MARK in source:
        if new not in source:
            raise RuntimeError('Incomplete RickyOS epdiy patch; inspect the SDK before building.')
        return source
    if source.count(old) != 1:
        raise RuntimeError('SDK epdiy source changed; review the 16-level commit before building.')
    return source.replace(old, new)


def apply(project_dir):
    base = Path(project_dir) / 'freeink-sdk/libs/display/EpdiyLcd'
    for path, old, new in ((base / 'src/EpdiyLcd.cpp', OLD_COMMIT, NEW_COMMIT),
                           (base / 'include/EpdiyLcd.h', OLD_DECL, NEW_DECL)):
        source = path.read_text()
        patched = patch_text(source, old, new)
        if patched != source:
            path.write_text(patched)


if 'Import' in globals():
    Import('env')
    if env.subst('$PIOENV') in ('rickyos_readpico', 'simulator_rickyos'):
        apply(env.subst('$PROJECT_DIR'))
elif __name__ == '__main__':
    apply(Path(__file__).resolve().parents[1])
