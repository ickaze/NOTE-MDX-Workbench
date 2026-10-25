$ErrorActionPreference = 'Stop'
$root = $env:WB_SOURCE_DIR
$records = @()
foreach ($language in @('ja','en')) {
    $target = Join-Path $root "build-ci/cpp-gui-$language"
    New-Item $target -ItemType Directory -Force | Out-Null
    Copy-Item "$root/bin/NOTE-MDX-Workbench.exe", "$root/bin/workbench_core.dll", "$root/bin/independent_opm.dll", "$root/bin/IndependentMDX-X68Bridge32.exe" $target -Force
    Copy-Item "$root/bin/pdx_languages" $target -Recurse -Force
    $settings = @{
        language=$language; mode='editor'; editor_name='editor'; source_path=''; pdx_path=''; watch=$false
        master_volume=137; pcm_volume=83; track_enabled=@($true,$false)*8; editor_top_line=0
        font_name='MS Gothic'; font_size=11; splitter_pos=510; wav_loop_count=3; wav_fade_ms=1234
        monitor_height=280; piano_roll=$true; carrier_pitch=$true; pitch_scroll=12; pitch_height=9
        editor_follow_mode=2; waveforms=$false; gutter_note_lengths=$true
        future_nested=@{name='保持'; enabled=$true; count=1234567890123456789}
    }
    $settings | ConvertTo-Json -Depth 5 | Set-Content "$target/NOTE-MDX-Workbench.json" -Encoding utf8NoBOM
    [System.IO.File]::WriteAllText("$target/editor_Autosave.mus", "; restored editor`nA c4`n", [System.Text.UTF8Encoding]::new($false))
    $process = Start-Process "$target/NOTE-MDX-Workbench.exe" -ArgumentList '--smoke' -WorkingDirectory $target -PassThru
    if (!$process.WaitForExit(120000)) {
        $process.Kill($true)
        throw "GUI smoke timed out: $language"
    }
    "C++ GUI exit: $($process.ExitCode)"
    Get-ChildItem "$target/GUI_SMOKE_*.json" | ForEach-Object { Get-Content $_ -Raw }
    if (Test-Path "$target/GUI_SMOKE_ERROR.txt") { Get-Content "$target/GUI_SMOKE_ERROR.txt"; throw "GUI smoke failed: $language" }
    if ($process.ExitCode -ne 0 -or !(Test-Path "$target/GUI_SMOKE_RESULT.json")) { throw "GUI smoke did not complete: $language" }
    $result = Get-Content "$target/GUI_SMOKE_RESULT.json" -Raw | ConvertFrom-Json
    if (!$result.ok -or $result.checks -ne 130 -or $result.wav_bytes -le 44 -or $result.version -ne '2.10.0' -or $result.implementation -ne 'C++' -or $result.abi_bytes -ne 16936) { throw "Invalid GUI smoke result: $language" }
    $saved = Get-Content "$target/NOTE-MDX-Workbench.json" -Raw | ConvertFrom-Json
    if ($saved.language -ne $language -or $saved.font_name -ne $settings.font_name -or $saved.font_size -ne $settings.font_size -or $saved.master_volume -ne 137 -or $saved.pcm_volume -ne 83) { throw "Settings round trip differs: $language" }
    foreach ($key in @('master_volume','pcm_volume','font_name','font_size','splitter_pos','wav_loop_count','wav_fade_ms','monitor_height','piano_roll','carrier_pitch','pitch_height','editor_follow_mode','waveforms','gutter_note_lengths')) {
        if ($result.startup_settings.$key -ne $settings.$key) { throw "Restored setting differs: $language / $key" }
    }
    if (($result.startup_settings.track_enabled -join ',') -ne ($settings.track_enabled -join ',')) { throw 'Restored track masks differ' }
    if ($result.startup_settings.editor_text.Replace("`r",'') -ne "; restored editor`nA c4`n") { throw 'Editor autosave not restored' }
    foreach ($object in @($result.startup_settings.future_nested, $saved.future_nested)) {
        if ($object.name -ne '保持' -or !$object.enabled -or $object.count -ne 1234567890123456789) { throw 'Nested future setting lost' }
    }
    $records += [ordered]@{language=$language; result=$result; settings_roundtrip=$true}
    "GUI $language : $($result.checks) checks, $($result.wav_bytes) WAV bytes" | Tee-Object -FilePath $env:GITHUB_STEP_SUMMARY -Append
}
if ($records[0].result.checks -ne $records[1].result.checks) { throw 'GUI language coverage differs' }
$records | ConvertTo-Json -Depth 8 | Set-Content "$root/build-ci/cpp-cpp-gui-summary.json" -Encoding utf8NoBOM

