"""Compare full controller command/data traces with the accepted dev16 SDK."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True, help='SDK with the accepted dev16 overlay restored')
args = parser.parse_args()
current = root / 'freeink-sdk'
traces = []
for sdk in (args.baseline.resolve(), current):
    display = sdk / 'libs/display/FreeInkDisplay'
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        for folder in ('driver', 'lut'):
            shutil.copytree(display / 'src' / folder, temp / folder)
        shutil.copytree(current / 'libs/display/FreeInkDisplay/test/host/ssd1677_stubs', temp, dirs_exist_ok=True)
        shutil.copy2(display / 'include/GrayscaleCapabilities.h', temp / 'GrayscaleCapabilities.h')
        panel = temp / 'driver/PanelDriver.h'
        panel.write_text(panel.read_text().replace('../../include/GrayscaleCapabilities.h', '../GrayscaleCapabilities.h'))
        binary = temp / 'trace'
        subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                        '-DFREEINK_DEVICE_METALIO_EINK4=1', '-DFREEINK_METALIO_UI_RETAINED=1',
                        '-I'+str(temp), '-I'+str(display / 'include'),
                        '-I'+str(sdk / 'libs/hardware/BoardConfig/include'),
                        str(Path(__file__).with_name('UiTrace.cpp')), '-o', str(binary)], check=True)
        traces.append(subprocess.check_output([str(binary)]))
assert traces[0] == traces[1], 'Controller traffic differs from the accepted waveform/selector baseline'
print('Accepted dev16/current UI controller traces are byte-identical:', len(traces[0]),
      'bytes; SHA-256', hashlib.sha256(traces[0]).hexdigest())
