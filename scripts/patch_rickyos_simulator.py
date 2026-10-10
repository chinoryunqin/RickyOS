"""Narrow, checked adaptation of the pinned simulator's task notification shim.

Only simulator_rickyos uses this. The maintained input is this script, not the
generated dependency under .pio/. Remove when the pinned shim honors timeouts.
"""

from pathlib import Path

OLD_NOTIFY = '''// Block until notified (simulates ulTaskNotifyTake with clear-on-exit).
inline uint32_t ulTaskNotifyTake(int /*clearOnExit*/,
                                 uint32_t /*ticksToWait*/) {
  auto *h = xTaskGetCurrentTaskHandle();
  std::unique_lock<std::mutex> lk(h->mtx);
  h->cv.wait(lk, [h] { return h->notifyCount > 0; });
  h->notifyCount--;
  return 1;
}'''

NEW_NOTIFY = '''// RICKYOS_TIMED_TASK_NOTIFY: FreeRTOS-compatible wait and counter semantics.
inline uint32_t ulTaskNotifyTake(int clearOnExit, uint32_t ticksToWait) {
  auto *h = xTaskGetCurrentTaskHandle();
  std::unique_lock<std::mutex> lk(h->mtx);
  const auto notified = [h] { return h->notifyCount > 0; };
  if (ticksToWait == portMAX_DELAY) {
    h->cv.wait(lk, notified);
  } else if (!h->cv.wait_for(lk, std::chrono::milliseconds(
                 static_cast<uint64_t>(ticksToWait) * portTICK_PERIOD_MS), notified)) {
    return 0;
  }
  const uint32_t count = h->notifyCount;
  if (clearOnExit) h->notifyCount = 0;
  else --h->notifyCount;
  return count;
}'''


def patch_task_notify(path):
    path = Path(path)
    source = path.read_text()
    if NEW_NOTIFY in source:
        return False
    if source.count(OLD_NOTIFY) != 1:
        raise RuntimeError('Simulator task shim changed; review the timed notification adapter.')
    path.write_text(source.replace(OLD_NOTIFY, NEW_NOTIFY))
    return True


if 'Import' in globals():
    Import('env')  # noqa: F821 -- supplied by PlatformIO/SCons
    if env.subst('$PIOENV') == 'simulator_rickyos':  # noqa: F821
        dependency = Path(env.subst('$PROJECT_LIBDEPS_DIR')) / 'simulator_rickyos' / 'simulator'  # noqa: F821
        patch_task_notify(dependency / 'src/freertos/task.h')
