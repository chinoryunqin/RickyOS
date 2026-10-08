"""Let the side key wake Read Pico from Standby's light sleep.

The CW32L010 PMU queues key events (KEY_DOWN, KEY_UP, KEY_SHORT, ...) in an
eight-deep FIFO and holds its interrupt line while any are pending. That line
reaches the host through the FCA9555, whose INT# (GPIO41) is the light-sleep
wake. The SDK reads the key as a level and never acknowledges the events, so
after a few presses the FIFO is full, the PMU line stays asserted, the expander
sees no further change, and a press no longer ends the sleep: Standby only woke
on its minute timer and missed the press. The reference firmware drains the
FIFO around every lock-screen sleep (main/sleep.c, read_pico_pmu.c
`read_pico_pmu_take_key_wakeup`); this exposes the same drain.

Like patch_rickyos_sdcard.py: a bounded mechanical rewrite, re-runnable, and the
build fails if the SDK source no longer matches instead of silently skipping it.
"""
from pathlib import Path

MARK = 'RICKYOS_PMU_KEY_EVENTS'

OLD_DECL = '''bool ioeIntAsserted();
void clearIoeInt();
'''
NEW_DECL = '''bool ioeIntAsserted();
void clearIoeInt();

// RICKYOS_PMU_KEY_EVENTS: acknowledge every queued PMU event so its interrupt
// line releases and the next press can pull INT# again. True when a press
// (KEY_DOWN or KEY_SHORT) was among them; keyDown is the key level right after.
bool takePmuKeyPress(bool& keyDown);
'''

OLD_DEF = '''bool sdCardPresent() {
  // Slot CD is active-low'''
NEW_DEF = '''// RICKYOS_PMU_KEY_EVENTS (read_pico_pmu.c `read_pico_pmu_take_key_wakeup`).
bool takePmuKeyPress(bool& keyDown) {
  constexpr uint8_t kKeyDown = 0x01;   // PMU_EVT_KEY_DOWN
  constexpr uint8_t kKeyShort = 0x03;  // PMU_EVT_KEY_SHORT
  constexpr int kFifoDepth = 8;        // PMU_EVENT_FIFO_DEPTH
  bool pressed = false;
  keyDown = false;
  if (!g_pmuPresent) return false;
  for (int i = 0; i < kFifoDepth; ++i) {
    if (!pmuPoll()) break;
    keyDown = g_pmuKeyDown;
    if (g_pmuPendingEvents == 0) break;
    uint16_t id = 0;
    uint8_t type = 0;
    if (!pmuPeekEvent(id, type)) break;
    if (type == kKeyDown || type == kKeyShort) pressed = true;
    if (!pmuEventAck(id)) break;
    delay(10);  // the PMU updates STATUS after an ACK (read_pico_pmu_event_ack)
  }
  return pressed;
}

bool sdCardPresent() {
  // Slot CD is active-low'''


def patch_text(source, old, new, mark=MARK):
    if new in source:
        return source
    if source.count(old) != 1:
        raise RuntimeError('SDK Read Pico board source changed; review the RickyOS patch (%s) before building.' % mark)
    return source.replace(old, new)


def apply(project_dir):
    board = Path(project_dir) / 'freeink-sdk/libs/hardware/BoardReadPico'
    for rel, old, new in (('include/BoardReadPico.h', OLD_DECL, NEW_DECL),
                          ('src/BoardReadPico.cpp', OLD_DEF, NEW_DEF)):
        path = board / rel
        source = path.read_text()
        patched = patch_text(source, old, new)
        if patched != source:
            path.write_text(patched)


if 'Import' in globals():
    Import('env')
    if env.subst('$PIOENV') == 'rickyos_readpico':
        apply(env.subst('$PROJECT_DIR'))
elif __name__ == '__main__':
    apply(Path(__file__).resolve().parents[1])
