#!/usr/bin/env python3
"""Test actual file publication, independent decoders and frozen Go output.

Compressed bytes may differ between Go DEFLATE and zlib. The uncompressed
VGM/GD3 and MRL must be byte-identical; containers retain the existing profile.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import io
import json
import os
import random
import shutil
import struct
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent

def decoded(data, mode):
    if mode == 2:
        assert data[:3] == b'\x1f\x8b\x08'
        assert data[4:8] == bytes(4) and data[9] == 255
        return gzip.decompress(data)
    if mode == 3:
        assert struct.unpack_from('<HHH', data, 4) == (45, 0x800, 8)
        assert data[18:26] == b'\xff'*8
        assert b'PK\x06\x06' in data and b'PK\x06\x07' in data
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            assert archive.namelist() == ['record.mrl']
            entry = archive.infolist()[0]
            assert entry.compress_type == zipfile.ZIP_DEFLATED
            assert entry.flag_bits == 0x800
            assert archive.testzip() is None
            return archive.read(entry)
    return data

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpp', required=True)
    parser.add_argument('--go', required=True)
    parser.add_argument('--report')
    args = parser.parse_args()
    executable = str(Path(args.cpp).resolve())
    counts = dict(go_comparisons=0, cancellation_points=0, failure_cases=0,
                  cross_volume_cases=0, publication_cases=0, compressed_patterns=0)
    result_digest = hashlib.sha256()
    hashes = json.loads((HERE/'output_oracle_hashes.json').read_text())
    with tempfile.TemporaryDirectory(prefix='workbench-output-test-') as root:
        root = Path(root)
        oracle_dir = root/'oracle'
        oracle_dir.mkdir()
        for name, digest in hashes.items():
            data = (HERE/'reference_go'/name).read_bytes()
            assert hashlib.sha256(data).hexdigest() == digest
            (oracle_dir/name[:-4]).write_bytes(data)
        shutil.copyfile(HERE/'output_oracle.go.txt', oracle_dir/'main.go')
        oracle = oracle_dir/'oracle'
        env = dict(os.environ, GO111MODULE='off')
        subprocess.run([args.go, 'build', '-trimpath', '-o', str(oracle), '.'],
                       cwd=oracle_dir, env=env, check=True)
        work = root/'作業 😀'
        work.mkdir()
        source = work/'入力.mrl'
        target = work/'出力 😀.bin'
        reference = work/'reference.bin'
        title = '日本語の曲名 🎵'
        sentinel = b'EXISTING FILE MUST SURVIVE'

        def invoke(mode, output=target, cancel=-1, input_path=source):
            result = subprocess.run([executable, str(input_path), str(output), str(mode), title, str(cancel)],
                                    cwd=work, capture_output=True)
            assert b'AddressSanitizer' not in result.stderr and b'runtime error:' not in result.stderr, result.stderr
            return result

        def clean():
            assert not list(work.glob('workbench-output-*'))

        rng = random.Random(291103)
        sizes = [0, 1, 63, 64, 65, 65535, 65536, 65537, 196609, 1048576]
        for size in sizes:
            raw = rng.randbytes(size)
            for mode in range(4):
                data = raw
                if mode in (1, 2):
                    if size < 64: continue
                    data = b'Vgm '+raw[4:]
                source.write_bytes(data)
                target.write_bytes(sentinel)
                result = invoke(mode)
                assert result.returncode == 0, result.stderr
                assert int(result.stdout.split()[1]) == 1000
                subprocess.run([str(oracle), str(source), str(reference), str(mode), title], check=True)
                actual = decoded(target.read_bytes(), mode)
                expected = decoded(reference.read_bytes(), mode)
                assert actual == expected, (size, mode)
                assert source.read_bytes() == data
                result_digest.update(actual)
                counts['go_comparisons'] += 1
                clean()
        # Compressible inputs must remain genuinely compressed, not merely wrapped.
        for mode in (2, 3):
            data = (b'Vgm '+bytes(60) if mode == 2 else b'') + b'MDXRLOG\0'*131072
            source.write_bytes(data)
            assert invoke(mode).returncode == 0
            assert len(target.read_bytes()) < len(data)//50
            counts['compressed_patterns'] += 1

        def every_cancel(output, cross=False):
            for mode in range(4):
                data = b'Vgm '+rng.randbytes(196605)
                source.write_bytes(data)
                output.write_bytes(sentinel)
                success = invoke(mode, output)
                assert success.returncode == 0, success.stderr
                calls = int(success.stdout.split()[0])
                for cancel in range(calls):
                    output.write_bytes(sentinel)
                    before = output.stat().st_ino
                    failure = invoke(mode, output, cancel)
                    assert failure.returncode == 1 and b'cancelled' in failure.stderr
                    assert output.read_bytes() == sentinel and output.stat().st_ino == before
                    assert source.read_bytes() == data
                    clean()
                    if cross:
                        assert not list(output.parent.glob('workbench-output-*'))
                        counts['cross_volume_cases'] += 1
                    counts['cancellation_points'] += 1
        every_cancel(target)

        # Error before and after the staging file exists must preserve old output.
        for mode in (1, 2):
            for bad in [b'', b'bad', bytes(64)]:
                source.write_bytes(bad); target.write_bytes(sentinel)
                assert invoke(mode).returncode == 1
                assert target.read_bytes() == sentinel
                clean(); counts['failure_cases'] += 1
        source.write_bytes(b'Vgm '+bytes(196605)); target.write_bytes(sentinel)
        for mode, output, input_path in [(99,target,source), (0,target,work/'missing'),
                (0,work/'missing-directory'/'out',source), (0,oracle_dir,source), (0,source,source)]:
            assert invoke(mode, output, input_path=input_path).returncode == 1
            assert target.read_bytes() == sentinel
            assert source.read_bytes().startswith(b'Vgm ')
            clean(); counts['failure_cases'] += 1
        alias = work/'hardlink'
        os.link(source, alias)
        assert invoke(0, alias).returncode == 1
        assert alias.read_bytes() == source.read_bytes()
        alias.unlink(); clean(); counts['failure_cases'] += 1

        # WAV publication moves an existing stage without adding another full copy
        # on the same volume. Cancellation leaves both names/bytes untouched.
        stage = work/'wav-stage'
        for cancel in (0, -1):
            stage.write_bytes(b'RIFF-stage'); target.write_bytes(sentinel)
            result = invoke('publish', cancel=cancel, input_path=stage)
            if cancel == 0:
                assert result.returncode == 1
                assert target.read_bytes() == sentinel and stage.read_bytes() == b'RIFF-stage'
            else:
                assert result.returncode == 0
                assert target.read_bytes() == b'RIFF-stage' and not stage.exists()
            counts['publication_cases'] += 1
        # Explicitly exercise the EXDEV copy/rename path when another filesystem exists.
        if Path('/dev/shm').is_dir() and Path('/dev/shm').stat().st_dev != work.stat().st_dev:
            with tempfile.TemporaryDirectory(prefix='workbench-output-test-', dir='/dev/shm') as other:
                cross_target = Path(other)/'出力.bin'
                every_cancel(cross_target, True)
                stage.write_bytes(b'RIFF-stage')
                cross_target.write_bytes(sentinel)
                assert invoke('publish', cross_target, input_path=stage).returncode == 0
                assert cross_target.read_bytes() == b'RIFF-stage'
                assert stage.read_bytes() == b'RIFF-stage'
                assert not list(Path(other).glob('workbench-output-*'))
                counts['publication_cases'] += 1
        clean()
    report = dict(reference_version='2.9.11', seed=291103, **counts,
                  decoded_sha256=result_digest.hexdigest(), reference_sha256=hashes,
                  mismatches=0, compressed_byte_equality_required=False)
    if args.report: Path(args.report).write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()
