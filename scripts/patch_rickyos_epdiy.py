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


# RICKYOS_PANEL_RESTART: after a TTF reading session the panel can show every frame with a
# lighter copy shifted along the scan (two screens superimposed) until a reboot, and full
# black/white refreshes do not clear it. Tearing the LCD/RMT scan path down and bringing
# it back up is the in-place equivalent of that reboot; Screen repair calls it first.
RESTART_MARK = 'RICKYOS_PANEL_RESTART'
OLD_RESTART_FN = 'void epdiyLcdEnd() {'
NEW_RESTART_FN = """// RICKYOS_PANEL_RESTART: rebuild the scan path with the same configuration. The
// framebuffers are reallocated, so the next frame is a clean full refresh.
bool epdiyLcdRestart() {
  if (!g_initialized || g_cfg == nullptr || g_nativeGrayActive) return false;
  const EpdiyLcdConfig* cfg = g_cfg;
  const uint16_t width = static_cast<uint16_t>(epd_width());
  const uint16_t height = static_cast<uint16_t>(epd_height());
  epdiyLcdEnd();
  g_railsUp = false;
  const bool ok = epdiyLcdBegin(*cfg, width, height, g_blackIsOne);
  esp_rom_printf("[EPDF] panel restart ok=%d\\r\\n", ok ? 1 : 0);
  return ok;
}

void epdiyLcdEnd() {"""

# RICKYOS_LQ_RESET: the two feed threads hand finished scan lines to the LCD interrupt
# through ring queues that were only reset after an error. When the CPU is loaded (a TTF
# page rasterizing, the next page prerendering, SD reads) the last refill interrupt of a
# phase can land after the LCD has stopped, leaving lines in a queue; the next phase then
# starts on those stale lines and every line of that thread is shifted. The offset differs
# from phase to phase, so the panel shows a lighter copy of the page shifted along the scan
# until the queues are rebuilt (reboot). Empty them before each phase, when both threads
# and the interrupt are idle; diagnostics builds report how often lines were left over.
LQ_MARK = 'RICKYOS_LQ_RESET'
OLD_LQ_RESET = '    ctx->lines_prepared = 0;\n    ctx->lines_consumed = 0;\n'
NEW_LQ_RESET = ('    // RICKYOS_LQ_RESET: drop lines a late interrupt left behind in the last phase.\n'
                '    for (int i = 0; i < NUM_RENDER_THREADS; i++) {\n'
                '        LineQueue_t* lq = &ctx->line_queues[i];\n'
                '        if (lq->bufs == NULL) continue;\n'
                '        int stale = lq->current - lq->last;\n'
                '        if (stale < 0) stale += lq->size;\n'
                '        if (stale > 0) {\n'
                '            rickyos_lq_stale_phases++;\n'
                '            if ((uint32_t)stale > rickyos_lq_stale_max) rickyos_lq_stale_max = (uint32_t)stale;\n'
                '        }\n'
                '        lq_reset(lq);\n'
                '    }\n'
                '    ctx->lines_prepared = 0;\n    ctx->lines_consumed = 0;\n')
OLD_LQ_FN = 'void IRAM_ATTR prepare_context_for_next_frame(RenderContext_t* ctx) {'
NEW_LQ_FN = ('// RICKYOS_LQ_RESET: phases that started with stale lines, and the most lines left over.\n'
             'static uint32_t rickyos_lq_stale_phases, rickyos_lq_stale_max;\n'
             'void epd_lq_stale_take(uint32_t* phases, uint32_t* max_lines) {\n'
             '    *phases = rickyos_lq_stale_phases;\n'
             '    *max_lines = rickyos_lq_stale_max;\n'
             '    rickyos_lq_stale_phases = 0;\n'
             '    rickyos_lq_stale_max = 0;\n'
             '}\n\n'
             'void IRAM_ATTR prepare_context_for_next_frame(RenderContext_t* ctx) {')
OLD_DIAG_PRINT = (
    '  esp_rom_printf("[EPDF] frame #%u mode=%u result=%u convert=%uus max_convert=%uus diff=%dms scan=%dms copy=%dms\\r\\n",\n'
    '                 static_cast<unsigned>(g_frameTiming.frames), static_cast<unsigned>(mode),\n'
    '                 static_cast<unsigned>(err), static_cast<unsigned>(g_frameTiming.convertUs),\n'
    '                 static_cast<unsigned>(g_frameTiming.maxConvertUs), diffMs, drawMs, copyMs);\n')
NEW_DIAG_PRINT = (
    '  uint32_t stalePhases = 0, staleMax = 0;\n'
    '  epd_lq_stale_take(&stalePhases, &staleMax);  // RICKYOS_LQ_RESET\n'
    '  esp_rom_printf("[EPDF] frame #%u mode=%u result=%u convert=%uus max_convert=%uus diff=%dms scan=%dms copy=%dms"\n'
    '                 " stale=%u/%u\\r\\n",\n'
    '                 static_cast<unsigned>(g_frameTiming.frames), static_cast<unsigned>(mode),\n'
    '                 static_cast<unsigned>(err), static_cast<unsigned>(g_frameTiming.convertUs),\n'
    '                 static_cast<unsigned>(g_frameTiming.maxConvertUs), diffMs, drawMs, copyMs,\n'
    '                 static_cast<unsigned>(stalePhases), static_cast<unsigned>(staleMax));\n')
OLD_DIAG_STATS = 'struct FrameTimingStats {'
NEW_DIAG_STATS = ('extern "C" void epd_lq_stale_take(uint32_t* phases, uint32_t* max_lines);  // RICKYOS_LQ_RESET\n'
                  'struct FrameTimingStats {')

# Header: every RickyOS declaration in one block after the commit declaration. Separate
# steps used to anchor on the same line and re-insert each other on every build; strip
# whatever earlier builds left and insert the block once.
HEADER_ANCHOR = 'bool epdiyLcdCommitGrayscale16(const uint8_t* bwProxy);\n'
HEADER_BLOCK = ('/// RICKYOS_GRAY16_TEXT_TURN: profile for the next 16-level commit\n'
                '/// (0 = Half, 1 = text turn, 2 = Full, 3 = Full, rails off).\n'
                'void epdiyLcdSetGray16Profile(uint8_t profile);\n'
                '/// RICKYOS_RAILS_IDLE: power the panel rails down after idleMs without a frame.\n'
                'void epdiyLcdRailsOffIfIdle(uint32_t idleMs);\n'
                '/// RICKYOS_PANEL_RESTART: rebuild the scan path; the next frame is a full refresh.\n'
                'bool epdiyLcdRestart();\n')
HEADER_LINES = set(HEADER_BLOCK.splitlines(keepends=True))


def patch_header(source):
    if source.count(HEADER_ANCHOR) != 1:
        raise RuntimeError('SDK epdiy header changed; review the RickyOS patch before building.')
    kept = ''.join(line for line in source.splitlines(keepends=True) if line not in HEADER_LINES)
    return kept.replace(HEADER_ANCHOR, HEADER_ANCHOR + HEADER_BLOCK)


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
                                   (OLD_IDLE_FN, NEW_IDLE_FN, RAILS_MARK),
                                   (OLD_RESTART_FN, NEW_RESTART_FN, RESTART_MARK),
                                   (OLD_DIAG_STATS, NEW_DIAG_STATS, LQ_MARK),
                                   (OLD_DIAG_PRINT, NEW_DIAG_PRINT, LQ_MARK)),
        base / 'src/epdiy/src/output_common/render_context.c': ((OLD_LQ_FN, NEW_LQ_FN, LQ_MARK),
                                                                (OLD_LQ_RESET, NEW_LQ_RESET, LQ_MARK)),
    }
    for path, steps in edits.items():
        source = path.read_text()
        patched = source
        for old, new, mark in steps:
            patched = patch_text(patched, old, new, mark)
        if patched != source:
            path.write_text(patched)
    header = base / 'include/EpdiyLcd.h'
    source = header.read_text()
    patched = patch_header(source)
    if patched != source:
        header.write_text(patched)


if 'Import' in globals():
    Import('env')
    if env.subst('$PIOENV') in ('rickyos_readpico', 'simulator_rickyos'):
        apply(env.subst('$PROJECT_DIR'))
elif __name__ == '__main__':
    apply(Path(__file__).resolve().parents[1])
