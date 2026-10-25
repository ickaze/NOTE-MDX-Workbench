# SPDX-License-Identifier: 0BSD
# Copyright (C) 2026 by I.C.KaZe
from pathlib import Path
import json
r=Path(__file__).resolve().parents[1]
a=r/'research/periodic'
trials=json.loads((a/'state_steps.json').read_text());vals=[];rows=[]
for t in trials:
 m=next(x for x in t['models'] if x['model']=='reverse4')
 seq=[1+int(v>.14) for v in t['changes_db']]
 rows.append((t['frequency'],m['phase'],len(vals),len(seq)));vals.extend(seq)
unit=json.loads((a/'am_fixed.json').read_text())['unit_db']
square=next(v for v in json.loads((a/'overview.json').read_text()) if v['name']=='38AMSQR')['results']
amp=[(s['amd'],round(s['db_quantiles'][2]/unit)) for s in square]
h='// SPDX-License-Identifier: 0BSD\n// Copyright (C) 2026 by I.C.KaZe\n// Measured step magnitudes and square-AM stable high levels, not emulator output.\n#pragma once\n#include <array>\nnamespace recording_vectors {\nstruct Step { unsigned char freq, origin; unsigned offset, length; };\n'
h+=f'inline constexpr std::array<unsigned char,{len(vals)}> increments = {{'+','.join(map(str,vals))+'};\n'
h+=f'inline constexpr std::array<Step,{len(rows)}> steps = {{{{'+','.join('{'+','.join(map(str,x))+'}' for x in rows)+'}};\n'
h+='struct Am { unsigned depth, units; };\n'
h+=f'inline constexpr std::array<Am,{len(amp)}> am = {{{{'+','.join('{'+','.join(map(str,x))+'}' for x in amp)+'}};\n}\n'
(r/'tests/periodic_recording_vectors.hpp').write_text(h)
