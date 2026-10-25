> Workbench収録範囲：この文書が挙げるresearch資料・録音・上流PACKAGE_MANIFEST.jsonは非同梱です。[資料の区別](../UPSTREAM_MATERIALS.md)を参照してください。記載の測定・検証は上流の該当段階の記録です。

# Noise normalization update — 0.12

Copyright (C) 2026 by I.C.KaZe

43NOISE records noise RMS / equivalent sine peak ≈0.24967 at TL0, NFRQ0.
The previous core used equal peak normalization; the noise branch now multiplies by 0.25.
This is an approximate measurement-derived correction, not a proof of raw integer chip output.
EG/AM mapping, TL endpoint quantization and random timing are unchanged.
All 43–46 scheduled windows are present, using the replacement recording 46.
See ../research/noise-validation-43-46/analysis/NOISE_VALIDATION.html.

Direct GCC builds passed noise normalization, key-on, state, and DLL/core equivalence tests.
The normalization regression fails against the previous core. CMake/CTest unavailable here.
Windows DLL rebuild and runtime tests were not performed. Core and DLL source share the fix.
API and state layouts unchanged; old state loaded with the new code renders corrected noise amplitude.

Measurements and functional tests were first completed in the 0.11b development snapshot; released as 0.12. Only version metadata was changed afterwards.
