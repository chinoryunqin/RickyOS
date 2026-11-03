#!/usr/bin/env python3
"""Compile actual camera power and NT26 wire code against bounded recording hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
test = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='metalio-peripheral-test-') as temp:
    executable = str(Path(temp) / 'test')
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(test/'stubs'), '-I'+str(root/'src'),
                    '-I'+str(root/'freeink-sdk/libs/hardware/MetalioCamera/src'),
                    '-I'+str(root/'freeink-sdk/libs/hardware/MetalioModem'),
                    str(test/'PeripheralTest.cpp'),
                    str(root/'freeink-sdk/libs/hardware/MetalioCamera/src/CameraPower.cpp'),
                    '-o', executable], check=True)
    subprocess.run([executable], check=True)
