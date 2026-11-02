#!/usr/bin/env python3
"""Read the diagnostic RAM windows with SBA, without halting/resetting the Plus."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import signal
import struct
import subprocess


FAULTS = {1: 'gauge_read_failed', 2: 'charging_current', 4: 'counter_reset',
          8: 'counter_jump', 16: 'external_power_or_unknown_during_standby'}
# Target a 30-minute run; allow endpoints missed by the existing 30 s awake poll.
MINIMUM_CAPTURE_SECONDS = 1500


def decode_sample(data):
    values = struct.unpack('<IHHhhHH', data)
    return dict(zip(['at_ms', 'counter_mah', 'mv', 'current_ma', 'average_ma', 'operation', 'valid'], values))


def decode_window(data, minimum_seconds):
    first, last = decode_sample(data[:16]), decode_sample(data[16:32])
    samples, faults, state = struct.unpack('<III', data[32:44])
    duration = (last['at_ms'] - first['at_ms']) & 0xffffffff
    delta = (last['counter_mah'] - first['counter_mah']) & 0xffff
    reasons = [name for flag, name in FAULTS.items() if faults & flag]
    if faults & ~sum(FAULTS):
        reasons.append('unknown_fault_bits')
    if state != 2:
        reasons.append('window_not_frozen')
    if samples < 2 or not first['valid'] or not last['valid']:
        reasons.append('missing_endpoints')
    if duration < minimum_seconds * 1000 or duration > 86400000:
        reasons.append('duration_outside_measurement_range')
    if delta > duration * 2000 // 3600000 + 2:
        reasons.append('implausible_counter_delta')
    result = dict(first=first, last=last, samples=samples, faults=faults, state=state,
                  duration_ms=duration, discharged_mah=delta, invalid_reasons=reasons,
                  valid=not reasons)
    if not reasons:
        step = 3600000 / duration
        result['counter_step_equivalent_ma'] = step
        result['indicated_average_discharge_ma'] = delta * step if delta else None
        result['counter_quantization_interval_ma'] = [max(0, delta - 1) * step, (delta + 1) * step]
        result['classification'] = 'coarse_gauge_measurement' if delta else 'below_counter_resolution'
    return result


def decode_capture(words, minimum_seconds=MINIMUM_CAPTURE_SECONDS):
    if len(words) != 27:
        raise ValueError('Expected 108 bytes of capture RAM')
    raw = struct.pack('<27I', *words)
    magic, version, cycle, mode, rearm = struct.unpack('<5I', raw[:20])
    if magic != 0x504f5752 or version != 1 or mode not in (0, 1):
        raise ValueError('Capture identity/layout mismatch')
    result = dict(schema_version=version, cycle=cycle, mode=['awake', 'standby'][mode], rearm=rearm,
                awake=decode_window(raw[20:64], minimum_seconds),
                standby=decode_window(raw[64:108], minimum_seconds),
                calibrated=False,
                limitation='Gauge indications only; current/coulomb deadbands and calibration error are not included '
                           'in the counter quantization interval. Zero counts do not prove zero current.')
    awake = result['awake']
    if (awake['state'] == 2 and awake['samples'] >= 2 and awake['faults'] == 0 and
            awake['first']['valid'] and awake['last']['valid'] and 60000 <= awake['duration_ms'] <= 86400000):
        last = awake['last']
        # A short awake interval supplies point/filtered readings, not a
        # charge-derived average. Never relax the mean-current validation above.
        result['awake_current_reference'] = dict(at_ms=last['at_ms'],
                instantaneous_current_ma=last['current_ma'], filtered_current_ma=last['average_ma'],
                mv=last['mv'], observed_window_ms=awake['duration_ms'],
                kind='point_and_filtered_readings_only', negative_means='discharge')
    return result


def symbol_address(map_text, symbol):
    matches = re.findall(r'^\s*(0x[0-9a-fA-F]+)\s+\S*' + re.escape(symbol) + r'\S*\s*$', map_text, re.M)
    if len(matches) != 1:
        raise ValueError('Missing or ambiguous map symbol: ' + symbol)
    return int(matches[0], 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', required=True, type=Path)
    parser.add_argument('--openocd', required=True, type=Path, help='OpenOCD installation directory')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--minimum-seconds', default=MINIMUM_CAPTURE_SECONDS, type=int)
    args = parser.parse_args()
    metadata = json.loads(args.metadata.read_text())
    if not re.fullmatch(r'[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}', metadata['device_mac']):
        raise SystemExit('Invalid device MAC')
    elf, map_file = Path(metadata['elf']), Path(metadata['map'])
    for path, key in [(elf, 'elf_sha256'), (map_file, 'map_sha256')]:
        if hashlib.sha256(path.read_bytes()).hexdigest() != metadata[key]:
            raise SystemExit('Build identity mismatch: ' + str(path))
    text = map_file.read_text()
    address = symbol_address(text, 'g_plusPowerCapture')
    summary = symbol_address(text, 'g_lastStandbySummary')
    available = symbol_address(text, 'g_haveStandbySummary')
    args.output.mkdir(parents=True, exist_ok=True)
    cfg = args.output / 'read-capture.cfg'
    cfg.write_text(f'''# Read RAM through the system bus. No halt/reset/flash or GDB attach.
set ESP_ONLYCPU 1
set ESP_RTOS none
set ESP_FLASH_SIZE 0
gdb port disabled
tcl port disabled
telnet port disabled
source [find board/esp32s31-builtin.cfg]
adapter serial {metadata['device_mac'].upper()}
$_TARGETNAME_0 riscv set_mem_access sysbus
init
echo "CPU_STATE [$_TARGETNAME_0 curstate]"
echo "POWER_CAPTURE [read_memory {address:#x} 32 27]"
echo "SLEEP_SUMMARY [read_memory {summary:#x} 32 6]"
echo "SUMMARY_AVAILABLE [read_memory {available:#x} 8 1]"
shutdown
''')
    command = [str(args.openocd / 'bin/openocd'), '-s', str(args.openocd / 'share/openocd/scripts'), '-f', str(cfg)]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output, _ = process.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        # Allow OpenOCD's shutdown handler to release the debug module first.
        process.send_signal(signal.SIGINT)
        try:
            output, _ = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()
        (args.output / 'jtag-power-capture.log').write_bytes(output)
        raise SystemExit('Debug read timed out; preserve the raw log')
    raw = output.decode(errors='replace')
    (args.output / 'jtag-power-capture.log').write_text(raw)
    if process.returncode or 'CPU_STATE running' not in raw:
        raise SystemExit('Read failed; preserve raw log: ' + raw)
    if f"serial ({metadata['device_mac'].upper()})" not in raw:
        raise SystemExit('Device identity mismatch; preserve raw log')
    match = re.search(r'^POWER_CAPTURE (.+)$', raw, re.M)
    if not match:
        raise SystemExit('Capture response missing')
    result = decode_capture([int(word, 16) for word in match[1].split()], args.minimum_seconds)
    result['version'] = metadata['version']
    result['device_mac'] = metadata['device_mac']
    sleep = re.search(r'^SLEEP_SUMMARY (.+)$', raw, re.M)
    if 'SUMMARY_AVAILABLE 0x1' in raw and sleep:
        s = [int(word, 16) for word in sleep[1].split()]
        if len(s) == 6:
            result['sleep_summary'] = dict(sleep_us=s[0] + (s[1] << 32), duration_ms=s[2],
                                           entered=s[3], rejected=s[4], sd_retained=s[5])
    output = args.output / 'power-capture.json'
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    print('Saved: ' + str(output))


if __name__ == '__main__':
    main()
