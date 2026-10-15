$ErrorActionPreference = 'Stop'
$taskBuildDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../examples/brookesia-minimal/build'))
$taskExportDir = Join-Path $taskBuildDir 'bootloader/bin'
New-Item -ItemType Directory -Force -Path $taskExportDir | Out-Null
$taskFlash = Get-Content -LiteralPath (Join-Path $taskBuildDir 'flasher_args.json') -Raw | ConvertFrom-Json
$taskHashes = @()
foreach ($taskEntry in $taskFlash.flash_files.PSObject.Properties) {
    $taskSource = Join-Path $taskBuildDir $taskEntry.Value
    $taskTarget = Join-Path $taskExportDir ([IO.Path]::GetFileName($taskEntry.Value))
    Copy-Item -LiteralPath $taskSource -Destination $taskTarget -Force
    $taskSourceHash = (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
    $taskTargetHash = (Get-FileHash -LiteralPath $taskTarget -Algorithm SHA256).Hash
    if ($taskSourceHash -ne $taskTargetHash) { throw "Export hash mismatch: $taskTarget" }
    $taskHashes += "$($taskEntry.Name) $([IO.Path]::GetFileName($taskTarget)) $taskTargetHash"
}
$taskHashes | Set-Content -LiteralPath (Join-Path $taskExportDir 'SHA256.txt') -Encoding UTF8
@'
ESP32-P4 / 16MB Flash / DIO / 80MHz
0x2000    bootloader.bin
0x8000    partition-table.bin
0x10000   ouse_brookesia_minimal.bin
0x810000  littlefs_data.bin

2026-10-15: UTF-8 password/SSID input, transparent themed Settings icons, corrected navigation row geometry.
Native services: MP3/16-bit PCM WAV playback and internal Flash log rotation.
PSRAM XiP enabled: Flash operations must remain compatible with external task stacks.
Repeated restart stability still requires device validation. Keep the matching ELF for new panic logs.
Desktop, Settings, gestures, themes, app lifecycle and logs passed host regressions.
Real speaker, SD removal and touchscreen behavior require device testing.
For this configuration/resource update, use all four files at the addresses above.
Reflashing LittleFS replaces installed apps, logs and user files. Back up data first.
Music, Logs, Counter and Files are not preinstalled. Install optional apps separately.
Music: examples/music/dist/net.0use.music-1.0.0.bpk
Logs: examples/log-viewer/dist/net.0use.logs-1.0.0.bpk
Install instructions: examples/music/README.md and examples/log-viewer/README.md
'@ | Set-Content -LiteralPath (Join-Path $taskExportDir (-join @([char]0x70e7,[char]0x5f55,[char]0x8bf4,[char]0x660e,'.txt'))) -Encoding UTF8
Write-Output "Verified export: $taskExportDir"
