#!/usr/bin/env python3
"""Compare C++ with fixed Go functions: v2.9.11 helpers and alpha.6 source positions.

The Go code is an executable test oracle, not a production dependency.
Run: python3 compare_helpers.py --cpp /path/to/frontend-compat --go /path/to/go
"""
from pathlib import Path
import argparse
import collections
import ctypes
import hashlib
import json
import os
import random
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SOURCES = [
    'editor_features.go', 'editor_tokens.go', 'editor_follow.go',
    'slider_position.go', 'control_events.go', 'pdx_project.go',
    'voice_document.go', 'vgm_metadata.go', 'editor_check.go', 'source_positions.go',
]

def verify_bridge(library, cases, expected):
    lib = ctypes.CDLL(str(Path(library).resolve()))
    call = lib.WB_FrontendText
    call.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t,
                     ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                     ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    call.restype = ctypes.c_int64
    opcodes = dict(normalize=1, compact=2, remainder=3, steps=4, notes=5,
                   token=6, key=8, slider=9, first=10, follow=11, restored=12,
                   click=13, pcm=14, reference=15, project=16, ensurepcm=17,
                   overwrite=18, append=19, scope=20, tag=21, location=22, character=23)
    gate_call = lib.WB_EditorGate
    gate_call.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_uint32,
        ctypes.c_int64, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_uint64)]
    gate_call.restype = ctypes.c_int
    mask = 2**64 - 1
    tested = 0
    for (command, args), want_hex in zip(cases, expected):
        if command == 'gate':
            state = (ctypes.c_uint64 * 4)()
            ticket = (ctypes.c_uint64 * 2)()
            tickets, output = [], []
            for action in args[0].decode().split(';'):
                f = action.split(',')
                if f[0] == 'change':
                    assert gate_call(state, 0, int(f[1])*1000000, 0, 0, 0, ticket) == 0
                    output.append('C;')
                elif f[0] == 'cancel':
                    assert gate_call(state, 1, 0, 0, 0, 0, ticket) == 0
                    output.append('X;')
                elif f[0] == 'take':
                    ok = gate_call(state, 2, int(f[1])*1000000, int(f[2]), 0, 0, ticket)
                    if ok:
                        tickets.append(tuple(ticket))
                        output.append(f'T:{ticket[0]},{ticket[1]};')
                    else: output.append('N;')
                else:
                    gen, rev = tickets[int(f[1])]
                    ok = gate_call(state, 3, 0, int(f[2]), gen, rev, ticket)
                    output.append(f'A:{ok};')
            assert ''.join(output).encode() == bytes.fromhex(want_hex.decode())
            tested += 1
            continue
        if command not in opcodes:
            continue
        left, right, numbers = b'', b'', []
        if command in ('normalize', 'pcm', 'reference', 'overwrite'):
            left = args[0]
        elif command in ('compact', 'remainder', 'first', 'key', 'slider', 'follow', 'click'):
            numbers = [int(v) for v in args]
        elif command in ('steps', 'notes'):
            numbers = [int(v) for v in args[0].split(b',')]
        elif command in ('location', 'character'):
            left, numbers = args[0], [int(v) for v in args[1:]]
        elif command == 'token':
            left, numbers = args[0], [int(args[1])]
        elif command == 'restored':
            present = args[0] != b'nil'
            numbers = [int(present), int(args[0]) if present else 0, int(args[1])]
        elif command in ('append', 'scope'):
            left = args[0]
            if command == 'append':
                right, numbers = args[1], [int(args[2])]
            else:
                numbers = [int(args[1])]
        else:
            left, right = args
        l = ctypes.create_string_buffer(left)
        r = ctypes.create_string_buffer(right)
        ns = (ctypes.c_uint64 * len(numbers))(*(n & mask for n in numbers))
        arguments = (opcodes[command], l, len(left), r, len(right), ns, len(numbers))
        required = call(*arguments, None, 0)
        assert 1 <= abs(required) < 128 * 1024 * 1024
        out = ctypes.create_string_buffer(abs(required) + 8)
        ctypes.memset(out, 0x5a, len(out))
        # Short buffers must remain completely untouched.
        if abs(required) > 1:
            assert call(*arguments, out, abs(required)-1) == required
            assert bytes(out) == b'Z' * len(out)
        assert call(*arguments, out, abs(required)) == required
        assert bytes(out)[abs(required):] == b'Z' * 8
        actual = bytes(out)[:abs(required)-1]
        if required < 0:
            actual = b'ERROR:' + actual
        assert actual == bytes.fromhex(want_hex.decode()), (command, args, actual, want_hex)
        tested += 1
    # Malformed ABI requests must report errors rather than dereference NULL.
    for op, left_size, count in [(999, 0, 0), (1, 1, 0), (4, 0, 16), (1, 0, 17)]:
        assert call(op, None, left_size, None, 0, None, count, None, 0) < 0
    # Nanosecond boundary and signed extremes are outside the millisecond oracle.
    for edit in [-2**63, -501000001, -1, 0, 1, 501000001, 2**63-500000001]:
        state = (ctypes.c_uint64 * 4)()
        ticket = (ctypes.c_uint64 * 2)()
        assert gate_call(state, 0, edit, 0, 0, 0, ticket) == 0
        assert gate_call(state, 2, edit+499999999, 7, 0, 0, ticket) == 0
        assert gate_call(state, 2, edit+500000000, 7, 0, 0, ticket) == 1
        assert tuple(ticket) == (1, 7)
        assert gate_call(state, 3, 0, 8, 1, 7, ticket) == 0
        assert gate_call(state, 3, 0, 7, 1, 7, ticket) == 1
        assert gate_call(state, 1, 0, 0, 0, 0, ticket) == 0
        assert gate_call(state, 3, 0, 7, 1, 7, ticket) == 0
        assert gate_call(state, 4, 0, 0, 0, 0, ticket) == 0
        assert gate_call(state, 2, 999999999, 7, 0, 0, ticket) == 0
    assert gate_call(None, 0, 0, 0, 0, 0, ticket) == -1
    assert gate_call(state, 0, 0, 0, 0, 0, None) == -1
    assert gate_call(state, 5, 0, 0, 0, 0, ticket) == -1
    return tested

def build_cases():
    rng = random.Random(2911)
    cases = []

    def add(command, *args):
        cases.append((command, tuple(a if isinstance(a, bytes) else str(a).encode('utf-8') for a in args)))

    limit = 2**64 - 1
    for n in list(range(0, 192 * 20)) + [limit, limit-1, 2**63, 192 * 1000000 + 16]:
        add('compact', n)
        add('remainder', n)
    for mask in range(65536):
        add('first', mask)
    for _ in range(512):
        values = [rng.choice([0, 1, 16, 24, 48, 192, limit, rng.randrange(2**32)]) for _ in range(16)]
        add('steps', ','.join(map(str, values)))
        add('notes', ','.join(map(str, values)))
    for mode in range(-3, 24):
        for mask in [0, 1, 3, 255, 256, 65534, 65535, 2**32-1]:
            add('follow', mode, mask)
        for previous in [0, 1]:
            add('restored', mode, previous)
    add('restored', 'nil', 0)
    add('restored', 'nil', 1)
    for note in range(0, 96):
        for width in [0, 1, 15, 56, 167, 168, 320, 1024, 1920, 3840]:
            add('key', note, width)
    for _ in range(1024):
        width = rng.randrange(-5, 4000)
        low = rng.randrange(-100, 10)
        high = rng.randrange(-10, 1000)
        add('slider', rng.randrange(-1000, 6000), rng.randrange(-100, 100), width, low, high)
    for id in [1012, 4100, 4104, 4105]:
        for code in [0, 1, 6, 768]:
            for source, button in [(0, 0), (0, 23), (23, 23), (22, 23), (2**32+1, 2**32+1)]:
                add('click', id, code, source, button)

    docs = [
        '', 'A c4', '\ufeffA c4\r\n\x1a', 'A\x1a c4', 'A c4\x1a \t\n',
        'A c4\x1a\u3000', '#title "曲名😀"\r\nA l8 c+d-e\r\n',
        '#pcmfile "BANK.PDX"', '#pcmfile "日本語 bank.pdx" ;comment',
        ' #pcmfile "disabled.pdx"\n// #pcmfile last.pdx',
        '#nlist\n#pcmfile ignored.pdx\n#list\n#PCMFILE active.pdx',
        '#pcmfile empty.pdx\n#pcmfile ""',
        '@1={\n#pcmfile fake.pdx\n}\nA @1 c4',
        '/* #overwrite\n#title fake\n*/\n#title real\nA c4',
        ';comment\n\n#title real', '#title no-newline', '#overwrite\nA c4\n',
        '#nlist\n#overwrite\n#list\nA c4',
        '#pcmfile\t\u3000"空 白.pdx"\u3000',
        'A あc8\nB 😀l0\r\nC n60,12',
        '\r\n\r\n', 'A\x00c4',
    ]
    atoms = ['A c4', '#title "test"', '#overwrite', '#nlist', '#list',
             '#pcmfile "bank.pdx"', '// #pcmfile "delay bank.pdx"',
             '@0={', '}', '#pcmfile ""', '/*', '*/', '; hi', '', ' ', '日本語😀']
    for _ in range(500):
        newline = rng.choice(['\n', '\r\n'])
        docs.append(newline.join(rng.choices(atoms, k=rng.randrange(0, 12))))
    for text in docs:
        add('normalize', text)
        add('pcm', text)
        add('reference', text)
        add('overwrite', text)
        for name in ['', 'bank', '日本語', 'dir/data.PDX', 'foo.pdxedit', 'abc.pdxed', 'a"b']:
            add('project', text, name)
            add('ensurepcm', text, name)
        for offset in [0, -12, 15]:
            add('scope', text, offset)
        for anchor in [-1, 0, 1, text.count('\n')+1, text.count('\n')+2]:
            add('append', text, '\u3000@1={ 1 }\r\n', anchor)
        units = len(text.encode('utf-16le')) // 2
        for begin in sorted(set([-1, 0, 1, units//2, units-1, units, units+1])):
            add('token', text, begin)
        ds = []
        for _ in range(12):
            ds.append(f'{rng.randrange(2)},{rng.randrange(-1,text.count(chr(10))+4)},{rng.randrange(-1,30)}')
        add('spans', text, ';'.join(ds))
        add('gutter', text, f'0,2,48;0,2,96;8,2,24;15,3,{limit};15,3,1;16,2,1;1,0,1')
    for _ in range(256):
        events = ';'.join(f'{rng.randrange(-1,18)},{rng.randrange(-1,10)},{rng.choice([0,1,limit,rng.randrange(2**32)])}' for _ in range(20))
        add('accumulate', events)
    for title in ['', 'title', '日本語😀', 'a\x00b']:
        for size in [0, 12, 63, 64, 128, 256, 1024]:
            data = b'Vgm ' + bytes(max(0, size-4)) if size >= 4 else bytes(size)
            add('tag', data, title)
    add('gate', 'change,0;take,499,1;change,450;take,949,2;take,950,2;accept,0,2;take,1000,2;change,1000;accept,0,3;accept,0,2;take,1500,3;accept,1,4;accept,1,3;cancel;accept,1,3;take,10000,3')
    for _ in range(256):
        now = rng.randrange(10000)
        revision = rng.randrange(2**32)
        add('gate', f'change,{now};take,{now+499},{revision};take,{now+500},{revision};accept,0,{revision};change,{now+501};accept,0,{revision};take,{now+1001},{revision+1};accept,1,{revision+1};cancel;take,{now+10000},{revision}')
    # Original Go position helpers with a deterministic byte-width callback.
    # Every UTF-16 boundary, including split CRLF and split surrogate pairs.
    texts = ['', 'A c4', 'A c4\r\nB r8', 'あｶ😀\rX\nY\r\n', '\n\r\r\n', 'a\x00b']
    for _ in range(100):
        texts.append(''.join(rng.choice(['a',' ', '\t', 'あ','ｶ','😀','\r','\n']) for _ in range(rng.randrange(40))))
    for text in texts:
        units = len(text.encode('utf-16-le')) // 2
        for offset in list(range(units + 3)) + [2**32-1]:
            add('location', text, str(offset))
        for line in range(-1, text.count('\n') + text.count('\r') + 4):
            for column in [-3,0,1,2,3,4,8,16,64]:
                add('character', text, str(line), str(column))
    large = 'A あc12 d8\r\n' * 256
    add('characterbatch', large, ';'.join(f'{line},{column}' for line in range(257,0,-1) for column in [1,3,4,5,8,32]))
    return cases

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpp', required=True)
    parser.add_argument('--go', default='go')
    parser.add_argument('--report')
    parser.add_argument('--bridge', help='Also test the exact C ABI shared library')
    args = parser.parse_args()
    expected_hashes = json.loads((HERE / 'oracle_hashes.json').read_text())
    for name in SOURCES:
        assert hashlib.sha256((HERE / 'reference_go' / (name+'.txt')).read_bytes()).hexdigest() == expected_hashes[name], f'Baseline changed: {name}'
    cases = build_cases()
    payload = ''.join(command + '\t' + '\t'.join(arg.hex() for arg in values) + '\n' for command, values in cases).encode()
    with tempfile.TemporaryDirectory(prefix='workbench-compat-') as tmp:
        path = Path(tmp)
        for name in SOURCES:
            shutil.copyfile(HERE / 'reference_go' / (name+'.txt'), path / name)
        shutil.copyfile(HERE / 'oracle_main.go.txt', path / 'oracle_main.go')
        oracle = path / 'oracle'
        env = os.environ.copy()
        env['GO111MODULE'] = 'off'
        subprocess.run([args.go, 'build', '-trimpath', '-o', str(oracle), '.'], cwd=path, env=env, check=True)
        expected = subprocess.run([str(oracle)], input=payload, stdout=subprocess.PIPE, check=True).stdout.splitlines()
        actual = subprocess.run([str(Path(args.cpp).resolve())], input=payload, stdout=subprocess.PIPE, check=True).stdout.splitlines()
    assert len(actual) == len(expected) == len(cases), (len(actual), len(expected), len(cases))
    failures = []
    for index, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            failures.append({'index': index, 'command': cases[index][0], 'args_hex': [p.hex() for p in cases[index][1]], 'go_hex': want.decode(), 'cpp_hex': got.decode()})
    report = {'reference_version': '2.9.11', 'position_reference_version': '2.10.0-alpha.6', 'test_count': len(cases), 'mismatches': len(failures),
              'commands': dict(sorted(collections.Counter(c for c, _ in cases).items())),
              'seed': 2911, 'reference_sha256': expected_hashes,
              'result_sha256': hashlib.sha256(b'\n'.join(expected)).hexdigest(), 'failures': failures[:20]}
    if args.bridge:
        report['abi_cases'] = verify_bridge(args.bridge, cases, expected)
    if args.report:
        Path(args.report).write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    if failures:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        raise SystemExit(1)
    print(f'Go / C++ differential: {len(cases)} cases, {len(report["commands"])} operations, zero mismatches')

if __name__ == '__main__':
    main()
