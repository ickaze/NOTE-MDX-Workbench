# Workbench integration

This directory contains the buildable source, C ABI, examples, tests and text documentation from independent-opm-0.14-source.zip. The Windows binary is supplied separately from independent-opm-0.14-windows-x64.zip and retained unchanged in bin/independent_opm.dll. Version: 0.14 / C ABI 1.

Hardware captures, recordings, research scripts and large media/reference files are omitted. References in upstream documents to those files describe the original upstream package; they are not included here. CMake build targets and the 12 regression tests are retained. Generated periodic test vectors are already in tests/; regenerating them from omitted research data is outside this subset.

Workbench preserves its C1/M2 KON adapter for 0.14, whose register semantics match 0.13. The compiler and API source are linked directly only for non-Windows validation; normal Windows playback loads the supplied DLL.

Workbench v2.9.11 edits documentation wording and relative links, identifies omitted research material, and preserves the provenance/licensing caveats. Historical hash JSONs and source code are unchanged. These documentation edits do not constitute new measurement validation or legal clearance.
