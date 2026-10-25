#!/usr/bin/env python3
"""Check the GUI PE layout, including the section gap regression from v2.9.9.
Uses Python's standard library; does not launch Windows code.
"""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]

def verify(data):
    def u16(at):
        return struct.unpack_from('<H', data, at)[0]

    def u32(at):
        return struct.unpack_from('<I', data, at)[0]

    def align(value, boundary):
        return (value + boundary - 1) // boundary * boundary

    assert data[:2] == b'MZ'
    pe = u32(0x3c)
    assert data[pe:pe+4] == b'PE\0\0'
    assert u16(pe+4) == 0x8664, 'Expected AMD64'
    assert u16(pe+22) & 2, 'Not an executable image'
    assert u32(pe+16) == 0, 'Unexpected COFF symbols'
    opt = pe+24
    assert u16(opt) == 0x20b and u16(opt+68) == 2, 'Expected PE32+ GUI'
    section_align, file_align = u32(opt+32), u32(opt+36)
    assert section_align >= 4096 and section_align & (section_align-1) == 0
    assert 512 <= file_align <= 65536 and file_align & (file_align-1) == 0
    image_size, headers = u32(opt+56), u32(opt+60)
    table = opt+u16(pe+20)
    count = u16(pe+6)
    assert 0 < count <= 96 and table+count*40 <= headers <= len(data)
    assert headers % file_align == 0
    expected = align(headers, section_align)
    sections = []
    raw_sections = []
    previous_raw_end = headers
    for index in range(count):
        at = table+index*40
        name, size, rva, raw_size, raw = struct.unpack_from('<8sIIII', data, at)
        label = name.rstrip(b'\0').decode('ascii')
        assert rva == expected, f'{label}: section RVA gap/overlap: expected {expected:#x}, got {rva:#x}'
        assert rva % section_align == 0
        if raw_size:
            assert raw % file_align == 0 and raw_size % file_align == 0
            assert raw >= previous_raw_end and raw+raw_size <= len(data)
            previous_raw_end = raw+raw_size
        assert not label.startswith('.debug'), 'Debug section remains'
        if label == '.symtab':
            # Go -s -w leaves just the four-byte empty COFF string table.
            assert size == 4 and data[raw:raw+4] == b'\x04\0\0\0'
        flags = u32(at+36)
        sections.append((rva, size, flags))
        raw_sections.append((rva, max(size, raw_size), raw, raw_size))
        expected = align(rva+max(size, raw_size), section_align)
    assert image_size == expected, 'SizeOfImage mismatch'
    entry = u32(opt+16)
    assert any(start <= entry < start+size and flags & 0x20000000 for start, size, flags in sections)
    assert u32(opt+108) == 16
    for index in range(16):
        rva, size = struct.unpack_from('<II', data, opt+112+index*8)
        if index == 6:
            if size:
                # MSVC emits type 13 section-layout records even with /DEBUG:NONE.
                # These are not PDB/CodeView, source-line or debug-symbol records.
                def file_offset(address):
                    for base, span, raw, raw_size in raw_sections:
                        if base <= address < base+span:
                            assert address-base < raw_size
                            return raw+address-base
                    raise AssertionError('Debug metadata RVA outside sections')
                begin = file_offset(rva)
                assert size % 28 == 0 and begin+size <= len(data)
                for entry in range(begin, begin+size, 28):
                    kind, length, raw = u32(entry+12), u32(entry+16), u32(entry+24)
                    assert kind == 13 and length >= 4 and raw+length <= len(data), 'Debug symbol record remains'
                    assert u32(raw) == 0, 'Unexpected section metadata signature' 
        if not size:
            continue
        if index == 4:
            assert rva+size <= len(data)
        else:
            assert any(start <= rva and rva+size <= start+length for start, length, _ in sections), f'Directory {index} outside sections'
    return table, count, section_align, opt

if __name__ == '__main__':
    paths = [Path(arg) for arg in sys.argv[1:] if arg != '--self-test']
    if not paths:
        paths = [ROOT / 'bin/NOTE-MDX-Workbench.exe']
    for path in paths:
        data = path.read_bytes()
        table, count, alignment, opt = verify(data)
        print(f'{path.name}: contiguous aligned PE sections, bounds, entry point and no debug symbols passed')
        if '--self-test' in sys.argv:
            broken = bytearray(data)
            at = table+(count-1)*40+12
            struct.pack_into('<I', broken, at, struct.unpack_from('<I', broken, at)[0]+alignment)
            try:
                verify(broken)
            except AssertionError as error:
                assert 'section RVA gap/overlap' in str(error), error
                print('Regression check: section gap rejected')
            else:
                raise AssertionError('Corrupt section gap was accepted')
