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

This build adds Waveshare-style screenshot recent tasks and an edge-back indicator.
Update BOTH 0x10000 and 0x810000 for this build.
Use all four files for the first flash or when changing partition layout.
Reflashing LittleFS replaces its installed applications and files. Back up data first.
Current features: desktop + Settings, power/battery, screenshot recent tasks + edge back.
Files is no longer preinstalled. Developer sources are retained in the repository.
Hardware behavior awaits device validation. Counter is not preinstalled.
'@ | Set-Content -LiteralPath (Join-Path $taskExportDir (-join @([char]0x70e7,[char]0x5f55,[char]0x8bf4,[char]0x660e,'.txt'))) -Encoding UTF8
Write-Output "Verified export: $taskExportDir"
