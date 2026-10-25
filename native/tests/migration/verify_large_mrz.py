#!/usr/bin/env python3
"""Optional >4 GiB sparse-file probe. Not part of the fast CTest suite.

The source is sparse on supporting filesystems; compression and decoding still
read more than 4 GiB. ZIP64 sizes, CRC and decoded SHA-256 are compared to Go.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpp', required=True)
    parser.add_argument('--go', required=True)
    parser.add_argument('--report', required=True)
    args = parser.parse_args()
    executable = str(Path(args.cpp).resolve())
    length = 2**32 + 65537
    footer = b'ZIP64 boundary preserved\0'
    with tempfile.TemporaryDirectory(prefix='workbench-large-mrz-') as directory:
        root = Path(directory)
        for name,digest in json.loads((HERE/'output_oracle_hashes.json').read_text()).items():
            data = (HERE/'reference_go'/name).read_bytes()
            assert hashlib.sha256(data).hexdigest() == digest
            (root/name[:-4]).write_bytes(data)
        shutil.copyfile(HERE/'output_oracle.go.txt', root/'main.go')
        oracle = root/'oracle'
        subprocess.run([args.go,'build','-trimpath','-o',str(oracle),'.'],cwd=root,
                       env=dict(os.environ,GO111MODULE='off'),check=True)
        source = root/'input.mrl'
        with source.open('wb') as file:
            file.seek(length-len(footer)); file.write(footer)
        cpp = root/'cpp.mrz'
        go = root/'go.mrz'
        subprocess.run([executable,str(source),str(cpp),'3',''],cwd=root,check=True)
        subprocess.run([str(oracle),str(source),str(go),'3',''],cwd=root,check=True)
        signatures = []
        sizes = []
        for archive in [cpp,go]:
            with archive.open('rb') as file:
                header=file.read(60)
            assert struct.unpack_from('<Q',header,44)[0] == length
            digest=hashlib.sha256()
            with zipfile.ZipFile(archive) as packed:
                assert packed.namelist()==['record.mrl']
                entry=packed.infolist()[0]
                assert entry.file_size==length and entry.compress_type==zipfile.ZIP_DEFLATED
                total=0
                tail=b''
                with packed.open(entry) as file:
                    while True:
                        block=file.read(1024*1024)
                        if not block: break
                        digest.update(block); total+=len(block)
                        tail=(tail+block)[-len(footer):]
                assert total==length and tail==footer
                signatures.append((entry.CRC,digest.hexdigest()))
                sizes.append(archive.stat().st_size)
        assert signatures[0]==signatures[1]
        assert not list(root.glob('workbench-output-*'))
    result=dict(input_bytes=length,crc32=signatures[0][0],
                decoded_sha256=signatures[0][1],cpp_mrz_bytes=sizes[0],go_mrz_bytes=sizes[1],
                decoded_match=True,zip64_size_match=True,crc_checked_by_decoder=True)
    Path(args.report).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
