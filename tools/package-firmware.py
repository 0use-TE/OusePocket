"""Package the successful root-project build for manual flashing elsewhere."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
OUT = ROOT / "tmp"
description = json.loads((BUILD / "project_description.json").read_text())
args = json.loads((BUILD / "flasher_args.json").read_text())
version = (ROOT / "version.txt").read_text().strip()
assert description["project_name"] == "ouse_core_desktop"
assert description["project_version"] == version
assert args["extra_esptool_args"]["chip"] == "esp32p4"
assert set(args["flash_files"]) == {"0x2000", "0x8000", "0x10000"}
OUT.mkdir(exist_ok=True)
files = list(args["flash_files"].values()) + ["flasher_args.json"]
for relative in files:
    source = (BUILD / relative).resolve()
    assert source.is_relative_to(BUILD.resolve()) and source.is_file()
    target = OUT / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    assert hashlib.sha256(source.read_bytes()).digest() == hashlib.sha256(target.read_bytes()).digest()

flash_arguments = " ".join(f'{offset} "{relative}"' for offset, relative in args["flash_files"].items())
settings = args["flash_settings"]
script = '''param([Parameter(Mandatory=$true)][string]$Port, [int]$Baud = 460800)
$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    python -m esptool --chip esp32p4 --port $Port --baud $Baud --before default-reset --after hard-reset write-flash '''
script += f'--flash-mode {settings["flash_mode"]} --flash-freq {settings["flash_freq"]} --flash-size {settings["flash_size"]} {flash_arguments}\n'
script += '''    if ($LASTEXITCODE -ne 0) { throw "esptool failed: $LASTEXITCODE" }
} finally { Pop-Location }
'''
(OUT / "flash.ps1").write_text(script, encoding="utf-8-sig")
(OUT / "README.md").write_text(f'''# Ouse Core {version} 烧录包

新轻量核心固件，工程：ouse_core_desktop。目标：微雪 ESP32-P4 WIFI6 Touch LCD 3.5，16MB Flash，芯片 revision >= 3.1。
包含顶部状态栏与应用网格、明暗主题保存、侧滑返回、下拉亮度面板、上滑最近应用、卡片切换/恢复/关闭、计数器与系统信息；不启动旧 Brookesia、USB 安装服务、Wi-Fi 或 BPK 应用。

## 图形工具单文件烧录

只勾选 `ouse-core-{version}-full.bin`，地址 **0x0**，DIO、80MHz，保持 DoNotChgBin 勾选，点击 START。
其他旧文件全部取消勾选，不要点击 ERASE。
合并文件的空洞填充为 0xFF，会重置原 NVS 设置；不写入 0x810000 起的 LittleFS。
已安装同分区布局的 0.1/0.2/0.3 固件，后续可只烧录 `ouse_core_desktop.bin` 到 **0x10000** 保留 NVS。

设置“外观”或下拉面板可切换浅/深色并自动保存。亮度拖动时立即生效，松手后异步保存；等提示“已保存，重启保留”再重启验收。
保存失败时可以点击“保存”重试。系统信息页显示的是 CPU 频率，RAM 为可分配堆的空闲/总量。

## 分文件烧录（保留 NVS）

复制本目录或解压固件 zip，在远程电脑进入解压目录执行（COM10 改成实际串口）：

```powershell
python -m pip install "esptool>=5,<6"
powershell -ExecutionPolicy Bypass -File .\\flash.ps1 -Port COM10
```

也可以在图形烧录工具选择 ESP32-P4、DIO、80MHz、16MB，并添加：

| 地址 | 文件 |
| --- | --- |
| 0x2000 | bootloader/bootloader.bin |
| 0x8000 | partition_table/partition-table.bin |
| 0x10000 | ouse_core_desktop.bin |

不执行全片擦除、不烧录 LittleFS。保留原有 LittleFS 分区，但本版不会加载里面的应用。
烧录后串口日志波特率为 115200，应看到 `OuseCore: DESKTOP READY; core={version}`。
桌面应出现设置和计数器两个图标；计数器可加一、清零、回桌面保留状态，关闭最近应用卡片后再打开归零。
左右边缘内滑返回；顶部下拉快捷面板；底部上滑最近应用，卡片左右滚动、点击恢复、上滑关闭。
设置仍可保存亮度并查看系统信息。物理屏幕和触摸需要你在板上验收。

SHA256SUMS.txt 是镜像校验清单。symbols/{version}/ 保存匹配 ELF/MAP，仅用于故障分析，不参与烧录，未收入传输 zip。
''', encoding="utf-8")
merged_name = f"ouse-core-{version}-full.bin"
merge_args = [sys.executable, "-m", "esptool", "--chip", "esp32p4", "merge-bin",
              "--output", str(OUT / merged_name), "--target-offset", "0x0",
              "--flash-mode", settings["flash_mode"], "--flash-freq", settings["flash_freq"],
              "--flash-size", settings["flash_size"]]
for offset, name in args["flash_files"].items():
    merge_args += [offset, str(OUT / name)]
subprocess.run(merge_args, check=True)
merged = (OUT / merged_name).read_bytes()
for offset, name in args["flash_files"].items():
    data = (OUT / name).read_bytes()
    address = int(offset, 0)
    assert merged[address:address+len(data)] == data
assert len(merged) < 0x810000
files.append(merged_name)
(OUT / "图形工具烧录说明.txt").write_text(
    f"新核心 {version}：只选择 {merged_name}，地址 0x0，DIO、80MHz，DoNotChgBin 勾选。\n"
    "其余文件全部取消勾选，点击 START，不点 ERASE。\n"
    "合并镜像会重置旧 NVS 设置，保留 LittleFS。后续保留设置升级可只烧录\n"
    "ouse_core_desktop.bin 到 0x10000（同分区布局）。\n"
    "亮度等显示已保存后重启测试；实机验收由用户完成。\n", encoding="utf-8-sig")
(OUT / "SHA256SUMS.txt").write_text("\n".join(
    hashlib.sha256((OUT / name).read_bytes()).hexdigest() + "  " + name for name in files
) + "\n", encoding="utf-8")
(OUT / "build-info.json").write_text(json.dumps({
    "project": description["project_name"], "version": version,
    "idf_version": description.get("idf_ver"), "target": "esp32p4",
    "flash_files": args["flash_files"], "device_tested": False,
}, indent=2), encoding="utf-8")
shutil.copy2(ROOT / "dependencies.lock", OUT / "dependencies.lock")
symbols = OUT / "symbols" / version
symbols.mkdir(parents=True, exist_ok=True)
for name in ["ouse_core_desktop.elf", "ouse_core_desktop.map", "config/sdkconfig.h"]:
    shutil.copy2(BUILD / name, symbols / Path(name).name)
transfer_files = files + ["flash.ps1", "README.md", "SHA256SUMS.txt", "build-info.json", "dependencies.lock", "图形工具烧录说明.txt"]
archive = OUT / f"ouse-core-{version}-firmware.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
    for name in transfer_files:
        z.write(OUT / name, name)
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
print(f"Packaged {description['project_name']} {version}: {archive}")
for offset, name in args["flash_files"].items():
    print(f"  {offset}: {name} ({(OUT/name).stat().st_size} bytes)")
