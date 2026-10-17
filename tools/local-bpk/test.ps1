param(
    [string]$Port,
    [ValidateSet('hello', 'list', 'install')][string]$Action = 'install',
    [string]$Package = 'resource-monitor'
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$env:PYTHONPATH = "$PSScriptRoot/vendor" + [IO.Path]::PathSeparator + $env:PYTHONPATH
$env:PYTHONUTF8 = '1'
$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCommand) { throw 'Python 3 is required. Install Python, then run this script again.' }
& python "$PSScriptRoot/check-ports.py"
if ($LASTEXITCODE -ne 0) { throw 'Python or bundled pyserial failed to load.' }
if (-not $Port) { $Port = Read-Host 'Enter the local board COM port (for example COM5)' }
if ($Port -notmatch '^COM[0-9]+$') { throw 'Expected a COM port such as COM5.' }
$logPath = Join-Path $PSScriptRoot ('test-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
"Port=$Port Action=$Action Package=$Package" | Out-File -LiteralPath $logPath -Encoding utf8
$ErrorActionPreference = 'Continue'
& python bpk.py --port $Port hello 2>&1 | Tee-Object -FilePath $logPath -Append
if ($LASTEXITCODE -ne 0) { Write-Output "Handshake failed. Log: $logPath"; exit 1 }
if ($Action -eq 'install') {
    if ($Package -notmatch '^[a-z0-9-]+$') { throw 'Invalid package name.' }
    & python bpk.py --port $Port install "packages/$Package.bpk" 2>&1 | Tee-Object -FilePath $logPath -Append
    if ($LASTEXITCODE -ne 0) { Write-Output "Install failed. Send this log: $logPath"; exit 1 }
}
if ($Action -ne 'hello') {
    & python bpk.py --port $Port list 2>&1 | Tee-Object -FilePath $logPath -Append
    if ($LASTEXITCODE -ne 0) { Write-Output "List failed. Log: $logPath"; exit 1 }
}
Write-Output "Finished. Log: $logPath"
