$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$bundleRoot = Join-Path $projectRoot 'dist/bpk-local-test'
New-Item -ItemType Directory -Force -Path $bundleRoot, "$bundleRoot/packages", "$bundleRoot/vendor" | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/bpk.py" -Destination $bundleRoot
Copy-Item -Path "$PSScriptRoot/local-bpk/*" -Destination $bundleRoot
$serialPath = & python -c 'import serial; print(serial.__path__[0])'
if ($LASTEXITCODE -ne 0) { throw 'Python with pyserial is required to package this tool.' }
Copy-Item -LiteralPath $serialPath.Trim() -Destination "$bundleRoot/vendor" -Recurse -Force
$sources = @('apps/resource-monitor', 'apps/api-test', 'apps/service-test', 'apps/music', 'apps/alarm', 'apps/countdown', 'apps/stopwatch', 'examples/calculator', 'examples/counter')
foreach ($source in $sources) {
    $name = Split-Path $source -Leaf
    & python "$PSScriptRoot/bpk.py" pack "$projectRoot/$source" "$bundleRoot/packages/$name.bpk"
    if ($LASTEXITCODE -ne 0) { throw "Failed to pack $source" }
}
Compress-Archive -Path "$bundleRoot/*" -DestinationPath "$projectRoot/dist/bpk-local-test.zip" -Force
Write-Output "Ready: $projectRoot/dist/bpk-local-test.zip"
