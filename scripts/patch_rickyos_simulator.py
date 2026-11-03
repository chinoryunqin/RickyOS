"""Checked adapters for timed task notification and retained native-gray frames.

Only simulator_rickyos uses this. The maintained input is this script, not the
generated dependency under .pio/. Remove each adapter when the pinned simulator
provides the matching capability.
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


RETAINED_GRAY = '''// RICKYOS_RETAINED_GRAY: reconstruct from the last visible frame, not staging pixels.
uint8_t* HalDisplay::beginRetainedGrayscale16() {
#if FREEINK_DEVICE_READPICO
  if (grayscale16Active || frameBufferLent || isInverted()) return nullptr;
  const std::lock_guard<std::mutex> lock(pixelBufMutex);
  grayscale16Active = true;
  for (size_t i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; ++i) {
    const uint8_t gray = static_cast<uint8_t>(pixelBuf[i] & 255);
    const uint8_t level = (gray + 8) / 17;
    const unsigned shift = (i & 1) * 4;
    grayscale16Buffer[i / 2] = (grayscale16Buffer[i / 2] & ~(15u << shift)) | (level << shift);
    const uint8_t bit = 0x80u >> (i & 7);
    if (level >= 8) frameBufferStorage[i / 8] |= bit;
    else frameBufferStorage[i / 8] &= ~bit;
  }
  return grayscale16Buffer.data();
#else
  return nullptr;
#endif
}

'''


def patch_retained_gray(dependency):
    dependency = Path(dependency)
    header = dependency / 'src/HalDisplay.h'
    source = header.read_text()
    declaration = '  uint8_t* beginRetainedGrayscale16();\n'
    anchor = '  uint8_t* beginGrayscale16();\n'
    if declaration not in source:
        if source.count(anchor) != 1:
            raise RuntimeError('Simulator native gray declaration changed')
        header.write_text(source.replace(anchor, anchor + declaration))
    driver = dependency / 'src/HalDisplay.cpp'
    source = driver.read_text()
    anchor = 'uint8_t* HalDisplay::beginGrayscale16() {\n'
    if RETAINED_GRAY not in source and 'SIMULATOR_DEVICE_METALIO_EINK4' not in source:
        if 'RICKYOS_RETAINED_GRAY' in source or source.count(anchor) != 1:
            raise RuntimeError('Simulator retained gray adapter changed')
        driver.write_text(source.replace(anchor, RETAINED_GRAY + anchor))


if 'Import' in globals():
    Import('env')  # noqa: F821 -- supplied by PlatformIO/SCons
    if env.subst('$PIOENV') in ('simulator_rickyos', 'simulator_rickyos_metalio_eink4'):  # noqa: F821
        dependency = Path(env.subst('$PROJECT_LIBDEPS_DIR')) / env.subst('$PIOENV') / 'simulator'  # noqa: F821
        clock_header = dependency / 'src/HalClock.h'
        source = clock_header.read_text()
        if 'setUseChinaServers' not in source:
            clock_header.write_text(source.replace('  void cancelSync()',
                '  void setUseChinaServers(bool) {}\n  void cancelSync()', 1))
        tilt = dependency / 'src/HalTiltSensor.h'
        source = tilt.read_text()
        if 'wasBackTappedNext' not in source:
            source = source.replace('  bool wasTiltedBack()',
                '  bool wasBackTappedNext() { return false; }\n  bool wasBackTappedPrevious() { return false; }\n  bool wasTiltedBack()')
            tilt.write_text(source)
        dns = dependency / 'src/DNSServer.h'
        source = dns.read_text()
        source = source.replace('void start(int port, const char *name, const IPAddress &ip) {}',
                                'bool start(int port, const char *name, const IPAddress &ip) { return true; }')
        source = source.replace('void start(int port, const char *name, const char *ip) {}',
                                'bool start(int port, const char *name, const char *ip) { return true; }')
        dns.write_text(source)
        clock = dependency / 'src/HalClock.h'
        source = clock.read_text()
        if 'cancelSync' not in source:
            source = source.replace('public:', 'public:\n  void cancelSync() {}', 1)
            clock.write_text(source)
        tilt = dependency / 'src/HalTiltSensor.h'
        source = tilt.read_text()
        if 'noteTouch' not in source:
            source = source.replace('class HalTiltSensor;',
                'namespace CrossPointBackTap { enum Value : uint8_t { TAP_OFF = 0, TAP_NEXT = 1, TAP_PREVIOUS = 2 }; }\nclass HalTiltSensor;', 1)
            source = source.replace('  bool wasTiltedForward()',
                '  void noteTouch() {}\n  void update(uint8_t mode, uint8_t orientation, bool inReader, uint8_t, uint8_t) { update(mode, orientation, inReader); }\n  bool wasTiltedForward()')
            tilt.write_text(source)
        http = dependency / 'src/SecureHttpClient.h'
        source = http.read_text()
        if 'getHost()' not in source:
            source = source.replace('  void setCACert',
                '  std::string getHost() const { return host_; }\n  void setCACert', 1)
            source = source.replace('    http_.begin(client_, url.c_str());',
                '    const std::string text(url.c_str());\n    const auto begin = text.find("://");\n    const auto start = begin == std::string::npos ? 0 : begin + 3;\n    host_ = text.substr(start, text.find_first_of("/:?", start) - start);\n    http_.begin(client_, url.c_str());')
            source = source.replace('private:', 'private:\n  std::string host_;', 1)
            http.write_text(source)
        display = dependency / 'src/HalDisplay.h'
        source = display.read_text()
        if 'lastUiGrayscaleRaster' not in source:
            source = source.replace('  uint8_t* beginGrayscale16();',
                '  const uint8_t* lastUiGrayscaleRaster() const { return nullptr; }\n  uint8_t* beginGrayscale16();')
            display.write_text(source)
        patch_task_notify(dependency / 'src/freertos/task.h')
        patch_retained_gray(dependency)

        if env.subst('$PIOENV') == 'simulator_rickyos_metalio_eink4':
            clock = dependency / 'src/HalClock.cpp'
            source = clock.read_text().replace('#if defined(SIMULATOR_DEVICE_READPICO) ||',
                '#if defined(SIMULATOR_DEVICE_METALIO_EINK4) || defined(SIMULATOR_DEVICE_READPICO) ||')
            clock.write_text(source)
            board = dependency / 'src/BoardConfig.h'
            source = board.read_text().replace('inline bool hasTouch() {',
                'inline bool hasTouch() {\n#if defined(SIMULATOR_DEVICE_METALIO_EINK4)\n  return true;\n#endif')
            # Idempotent on subsequent builds.
            if source.count('defined(SIMULATOR_DEVICE_METALIO_EINK4)') > 1:
                source = board.read_text()
            board.write_text(source)

            # Enable the host's 4bpp working raster at 800x480; quantize its
            # submitted output to the ordinary board's four calibrated tones.
            for filename in ('HalDisplay.h', 'HalDisplay.cpp'):
                path = dependency / 'src' / filename
                source = path.read_text().replace('#if FREEINK_DEVICE_READPICO\n',
                    '#if FREEINK_DEVICE_READPICO || defined(SIMULATOR_DEVICE_METALIO_EINK4)\n')
                source = source.replace('return BoardConfig::ACTIVE.grayscaleLevels;', 'return 16;')
                source = source.replace('pixelBuf[i] = argbGray(level * 17);',
                    'pixelBuf[i] = argbGray(((level + 2) / 5) * 85);')
                path.write_text(source)
