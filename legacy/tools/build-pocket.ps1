$ErrorActionPreference = 'Stop'
$env:IDF_PATH = 'D:\esp\v6.1\esp-idf'
$env:IDF_TOOLS_PATH = 'D:\Espressif'
$env:IDF_PYTHON_ENV_PATH = 'D:\Espressif\python_env\idf6.1_py3.11_env'
$env:ESP_IDF_VERSION = '6.1.0'
$env:PYTHONUTF8 = '1'
$env:PATH = 'D:/Espressif/python_env/idf6.1_py3.11_env/Scripts;D:/Espressif/tools/cmake/4.0.3/bin;D:/Espressif/tools/ninja/1.12.1;D:/Espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin;' + $env:PATH
Push-Location (Join-Path $PSScriptRoot '../examples/brookesia-minimal')
try {
    # Windows PowerShell turns redirected native stderr warnings into error records.
    $ErrorActionPreference = 'Continue'
    & "$env:IDF_PYTHON_ENV_PATH/Scripts/python.exe" -u "$env:IDF_PATH/tools/idf.py" build *> build/build-agent.log
    $taskBuildResult = $LASTEXITCODE
    Get-Content build/build-agent.log -Tail 12
} finally { Pop-Location }
exit $taskBuildResult
