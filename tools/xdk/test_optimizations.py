#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exhaustively compare direct ARM routing with NooDS's 4096-entry table."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from optimize_noods import ROOT, decoder, arm_rows

arm = ROOT / 'xdk/noods/core/arm'
text = (arm / 'interpreter_lookup.cpp').read_text()
table = text.split('Interpreter::armInstrs[])(uint32_t) = {', 1)[1].split('};', 1)[0]
entries = re.findall(r'&Interpreter::(\w+)', table)
handlers = {name: i for i, name in enumerate(sorted(set(entries)))}
generated = decoder(text)
assert generated == (arm / 'arm_direct.inc').read_text(), 'Regenerate arm_direct.inc'
rows = arm_rows(text)
# Exercise the actual C++ include, including both values of the build toggle.
with tempfile.TemporaryDirectory(prefix='xds-arm-routing-') as temp:
    temp = Path(temp)
    (temp / 'arm_direct.inc').write_text(generated)
    functions = '\n'.join('int %s(unsigned int) { return %d; }' % item for item in handlers.items())
    expected = ','.join(str(handlers[name]) for name in entries)
    accelerated = ','.join(str(int(row in rows)) for row in range(256))
    (temp / 'test.cpp').write_text('''
struct Decoder {
%s
int run(unsigned int opcode) {
(void)opcode;
#include "arm_direct.inc"
return -1;
}
};
int main() {
int expected[] = {%s};
int accelerated[] = {%s};
Decoder decoder;
for (unsigned int index = 0; index < 4096; ++index) {
    unsigned int opcode = ((index >> 4) << 20) | ((index & 15) << 4);
    int actual = decoder.run(opcode);
#if XDS_ARM_DIRECT
    if (accelerated[index >> 4]) { if (actual != expected[index]) return 1; }
    else if (actual != -1) return 2;
#else
    if (actual != -1) return 3;
#endif
}
}
''' % (functions, expected, accelerated))
    for enabled in (0, 1):
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++03', '-Wall', '-Wextra',
                        '-Wno-unused-variable', '-Wno-unused-but-set-variable',
                        '-DXDS_ARM_DIRECT=%d' % enabled,
                        str(temp / 'test.cpp'), '-o', str(temp / 'test')], check=True)
        subprocess.run([str(temp / 'test')], check=True)
print('PASS: 4096 ARM routes, direct/table toggles; %d accelerated rows' % len(rows))
