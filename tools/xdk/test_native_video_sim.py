#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""After sim/run_sim.sh: compare native and CPU presentation on stripe fixtures.

This tests the simulator integration, not compilation/execution of Xenos HLSL.
"""
from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import tempfile
from PIL import Image, ImageChops

build = Path(os.environ.get('TMPDIR', '/tmp')) / 'xds360-sim-build'
binary = build / 'xds360-sim'
if not binary.exists():
    raise SystemExit('Run tools/xdk/sim/run_sim.sh first')
script = 'wait:20 press:DOWN wait:3 press:DOWN wait:3 press:A wait:330 shot:compare press:RS wait:10'
with tempfile.TemporaryDirectory(prefix='xds-native-sim-') as temp:
    temp = Path(temp)
    for filtering in (0, 2):
        shots = []
        for mode in ('native', 'disabled', 'shader_failure'):
            folder = temp / ('filter%d-%s' % (filtering, mode))
            shutil.copytree(build / 'console', folder)
            # All fixtures were made by make_noods_smoke_rom.py. Alternating
            # red/blue pixels reveal wrong swizzles and packed-byte filtering.
            for rom in (folder / 'roms').rglob('*.nds'):
                data = rom.read_bytes()
                rom.write_bytes(data.replace(struct.pack('<I', 0x001F001F), struct.pack('<I', 0x7C00001F)))
            ini = folder / 'noods.ini'
            text = ini.read_text()
            for key, value in (('screenFilter', filtering), ('gpuNativeVideo', int(mode != 'disabled'))):
                text, found = re.subn(r'^' + key + r'=.*$', key + '=' + str(value), text, flags=re.M)
                if not found: text += '\n' + key + '=' + str(value) + '\n'
            ini.write_text(text)
            env = os.environ.copy()
            env.update(SIM_ROOT=str(folder), SIM_SHOTS=str(folder), SIM_SCRIPT=script)
            env.pop('SIM_FAIL_NATIVE_SHADERS', None)
            if mode == 'shader_failure': env['SIM_FAIL_NATIVE_SHADERS'] = '1'
            result = subprocess.run([str(binary)], env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=60)
            if result.returncode: raise SystemExit(result.stdout.decode(errors='replace'))
            log = (folder / 'xds360.log').read_text()
            counts = [int(x) for x in re.findall(r' native=(\d+)', log)]
            assert counts, 'No performance window in ' + mode
            assert any(counts) if mode == 'native' else not any(counts), log
            if mode == 'shader_failure': assert 'point=unavailable linear=unavailable' in log
            shots.append(Image.open(folder / 'compare.ppm').convert('RGB'))
        for fallback in shots[1:]:
            assert ImageChops.difference(shots[0], fallback).getbbox() is None, 'Presentation mismatch'
        print('PASS simulator filter=%d: striped native, disabled and shader-failure images match' % filtering)
