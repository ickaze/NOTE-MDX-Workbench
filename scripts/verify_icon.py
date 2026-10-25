#!/usr/bin/env python3
"""Verify the EXE's actual icon resources against the source ICO (stdlib only)."""
from pathlib import Path
import struct
import sys

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'bin/NOTE-MDX-Workbench.exe'
data = exe.read_bytes()

def u16(offset):
    return struct.unpack_from('<H', data, offset)[0]

def u32(offset):
    return struct.unpack_from('<I', data, offset)[0]

pe = u32(0x3c)
assert data[pe:pe+4] == b'PE\0\0'
optional = pe + 24
assert u16(optional) == 0x20b, 'Expected PE32+ x64'
section_table = optional + u16(pe + 20)
sections = []
for index in range(u16(pe + 6)):
    at = section_table + index * 40
    sections.append((u32(at+12), max(u32(at+8), u32(at+16)), u32(at+20)))

def file_offset(rva):
    for start, size, raw in sections:
        if start <= rva < start + size:
            return raw + rva - start
    raise AssertionError('RVA outside PE sections')

base = file_offset(u32(optional + 112 + 2 * 8))
resources = {}

def visit(relative, path=()):
    at = base + relative
    count = u16(at + 12) + u16(at + 14)
    for index in range(count):
        key, target = struct.unpack_from('<II', data, at + 16 + index * 8)
        assert not key & 0x80000000, 'Unexpected named icon resource'
        current = path + (key,)
        if target & 0x80000000:
            visit(target & 0x7fffffff, current)
        else:
            entry = base + target
            offset = file_offset(u32(entry))
            resources[current] = data[offset:offset+u32(entry+4)]

visit(0)
groups = [(path, value) for path, value in resources.items() if path[:2] == (14, 101)]
assert len(groups) == 1, 'Application group icon 101 missing or duplicated'
path, group = groups[0]
ico = (root / 'assets/app.ico').read_bytes()
assert struct.unpack_from('<HH', group) == (0, 1)
count = struct.unpack_from('<H', group, 4)[0]
assert count == struct.unpack_from('<H', ico, 4)[0] == 10
sizes = []
for index in range(count):
    at = 6 + index * 14
    width, height, colors, reserved, planes, bits, length, icon_id = struct.unpack_from('<BBBBHHIH', group, at)
    source = 6 + index * 16
    assert group[at:at+4] == ico[source:source+4]
    assert planes == 1  # windres normalizes ICO's unspecified (zero) planes.
    assert group[at+6:at+12] == ico[source+6:source+12]
    start = struct.unpack_from('<I', ico, source+12)[0]
    assert resources[(3, icon_id, path[2])] == ico[start:start+length]
    sizes.append((width or 256, height or 256))
assert sizes == [(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)]
print('Embedded group icon 101: all 10 sizes byte-identical to assets/app.ico')
