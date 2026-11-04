"""Disabled peripherals must link and remain inert without their hardware SDKs."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts'))
from metalio_module_config import modules_from_defines
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / 'BoardConfig.h').write_text('#pragma once\n')
    for board in (0, 1):
        binary = temp / f'modules-{board}'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', f'-DFREEINK_DEVICE_METALIO_EINK4={board}',
                        '-DRICKYOS_METALIO_CAMERA=0', '-DRICKYOS_METALIO_CELLULAR=0',
                        '-I'+str(temp), '-I'+str(root / 'lib/hal'),
                        str(Path(__file__).with_name('DisabledModulesTest.cpp')),
                        str(root / 'lib/hal/HalCamera.cpp'), str(root / 'lib/hal/HalCellular.cpp'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    # Compare Python/SCons dependency decisions with the C++ HAL policy. This
    # catches drift in defaults and all independent module combinations.
    probe = temp / 'policy.cpp'
    probe.write_text('#include "MetalioFeatures.h"\nint main(){return '
                     'RICKYOS_METALIO_CAMERA_ENABLED + 2*RICKYOS_METALIO_CELLULAR_ENABLED;}\n')
    for board in (0, 1):
        for camera, cellular in [(None, None), (0, 0), (1, 0), (0, 1), (1, 1)]:
            defines = [('FREEINK_DEVICE_METALIO_EINK4', str(board))]
            if camera is not None:
                defines += [('RICKYOS_METALIO_CAMERA', str(camera)),
                            ('RICKYOS_METALIO_CELLULAR', str(cellular))]
            binary = temp / 'policy'
            subprocess.run(['c++', '-std=c++17', '-I'+str(temp), '-I'+str(root/'lib/hal'),
                            *['-D'+name+'='+value for name, value in defines],
                            str(probe), '-o', str(binary)], check=True)
            actual = subprocess.run([str(binary)]).returncode
            expected = modules_from_defines(defines)
            assert actual == int(expected['camera']) + 2*int(expected['cellular'])
print('Disabled camera/cellular: actual HAL links without device SDKs; inert lifecycle passed')
print('Build/HAL module policy: defaults and independent combinations agree')
