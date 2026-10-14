$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskReference = Join-Path $taskRoot '.reference/esp-brookesia'
$taskCommit = 'e937455b0db1a3e873b1da61d6d13652f3dcc7e2'
if (-not (Test-Path -LiteralPath $taskReference)) {
    git clone --no-checkout --depth 1 https://github.com/espressif/esp-brookesia.git $taskReference
    if ($LASTEXITCODE -ne 0) { throw 'Clone failed' }
    git -C $taskReference fetch --depth 1 origin $taskCommit
    if ($LASTEXITCODE -ne 0) { throw 'Fetch failed' }
    git -C $taskReference checkout --detach $taskCommit
    if ($LASTEXITCODE -ne 0) { throw 'Checkout failed' }
}
$taskActual = git -C $taskReference rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $taskActual -ne $taskCommit) {
    throw 'Reference checkout differs from the tutorial snapshot. Preserve changes and inspect it manually.'
}
$taskDirty = git -C $taskReference status --porcelain
if ($taskDirty) { throw 'Reference checkout has local changes; inspect before proceeding.' }
Write-Output "Brookesia snapshot ready: $taskCommit"
Write-Output 'Next: build the desktop firmware using tutorial section 6. Host app tools can wait.'
