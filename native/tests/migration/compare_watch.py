#!/usr/bin/env python3
"""Compare native state decisions with the fixed alpha.7 Go watch body."""
from pathlib import Path
import argparse, hashlib, json, os, random, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--cpp',required=True);p.add_argument('--go',required=True);p.add_argument('--report',required=True);a=p.parse_args()
here=Path(__file__).resolve().parent
reference=(here/'watch_reference.go.txt').read_bytes()
record=json.loads((here/'watch_reference_hash.json').read_text())
assert hashlib.sha256(reference).hexdigest()==record['watch_reference.go.txt']
steps=[('missing','10'),('0','0'),('0','0'),('1','0'),('0','0'),('0','1'),('0','0'),('0','1'),('missing','999'),('0','missing'),('0','1')]
rng=random.Random(291108)
for _ in range(256):
 steps.append(('reset','reset'))
 for _ in range(64):
  steps.append(tuple(rng.choice(['missing','0','1','10',str(2**63-1),str(rng.randrange(2**63))]) for _ in range(2)))
data=''.join(s+' '+p+'\n' for s,p in steps).encode()
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp);(root/'main.go').write_bytes(reference)
 env=os.environ.copy();env['GO111MODULE']='off'
 subprocess.run([a.go,'build','-trimpath','-o',str(root/'oracle'),'.'],cwd=root,env=env,check=True)
 expected=subprocess.check_output([str(root/'oracle')],input=data)
 actual=subprocess.check_output([str(Path(a.cpp).resolve())],input=data)
assert expected==actual,'source watch state mismatch'
report={'reference_version':record['reference_version'],'observations':len(expected.splitlines()),'resets':256,'seed':291108,'mismatches':0,'reference_sha256':record['watch_reference.go.txt'],'result_sha256':hashlib.sha256(expected).hexdigest(),'scope':'synthetic timestamp decisions; Windows filesystem path/handle execution separate'}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('Source watch Go/C++:',report['observations'],'observations, zero mismatches')
