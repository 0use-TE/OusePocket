"""Package the successful root-project build for manual flashing elsewhere."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile
import subprocess
import sys
import yaml

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
OUT = ROOT / "tmp"
description = json.loads((BUILD / "project_description.json").read_text())
args = json.loads((BUILD / "flasher_args.json").read_text())
version = (ROOT / "version.txt").read_text().strip()
locked = yaml.safe_load((ROOT / "dependencies.lock").read_text(encoding="utf-8"))
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
包含原生桌面/设置/主题/手势，以及 JSON＋JS 计数器、资源监视、接口测试应用，以及 dp/sp、中英文即时切换、字号设置和变化通知；增加 PMIC 电源/自动熄屏、ES8311 录放音、C6 Wi-Fi Remote；不启动旧 Brookesia；新增独立 USB/UART0 BPK 安装服务。

## 图形工具单文件烧录

只勾选 `ouse-core-{version}-full.bin`，地址 **0x0**，DIO、80MHz，保持 DoNotChgBin 勾选，点击 START。
其他旧文件全部取消勾选，不要点击 ERASE。
合并文件的空洞填充为 0xFF，会重置原 NVS 设置；不写入 0x810000 起的 LittleFS。
已安装同分区布局的 0.1/0.2/0.3/0.4 固件，后续可只烧录 `ouse_core_desktop.bin` 到 **0x10000** 保留 NVS。

设置“外观”或下拉面板可切换浅/深色。设置即时生效、后台自动保存；亮度松手保存，音量请求合并保存。成功不显示页脚，不再有保存按钮。
保存失败时在对话框里重试。系统信息页显示的是 CPU 频率，RAM 为可分配堆的空闲/总量。

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

不执行全片擦除、不烧录 LittleFS。保留原有 LittleFS 分区，本版仅加载 /ouse-apps/ 内的 BPK。
烧录后串口日志波特率为 921600，应看到 `OuseCore: DESKTOP READY; core={version}`。
桌面应出现设置、计数器、资源监视、接口测试四个图标；计数器可加一、清零、回桌面保留状态，关闭最近应用卡片后再打开归零。
左右边缘内滑返回；顶部下拉快捷面板，短拉回弹；滑块调亮度不触发收起。
底部上滑后松手回桌面，上滑后停留 260ms 打开最近应用；卡片左右居中吸附、点击恢复、上滑关闭，短滑回弹。
最近应用底部“全部清除”关闭实例，点卡片外空白回桌面；清除后桌面应用图标仍保留。
资源监视应立即显示数值，滑块可改变刷新周期；后台暂停，恢复继续。
设置首页为机型卡和分组列表，增加电源、声音、Wi-Fi 子页；侧滑先回设置首页。
Wi-Fi 默认关闭；开启后后台启动 C6 SDIO，无线连接成功后自动记住网络。
电源可设置自动熄屏，触摸唤醒不误点；声音可调音量/静音、试听、3 秒录音和回放。
0.4.1 已由用户确认正常；本版手势、动画和设置布局需要实机验收。页面可上下滚动；语言/字号立即生效，设置后稍等再正常重启检查恢复。物理屏幕/触摸、电池、扬声器、麦克风、C6 连接与密码错误恢复需要板上验收。

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
    "设置后稍等再正常重启测试；实机验收由用户完成。\n", encoding="utf-8-sig")
(OUT / "SHA256SUMS.txt").write_text("\n".join(
    hashlib.sha256((OUT / name).read_bytes()).hexdigest() + "  " + name for name in files
) + "\n", encoding="utf-8")
(OUT / "build-info.json").write_text(json.dumps({
    "project": description["project_name"], "version": version,
    "idf_version": description.get("idf_ver") or locked["dependencies"]["idf"]["version"], "target": "esp32p4",
    "flash_files": args["flash_files"], "device_tested": False,
}, indent=2), encoding="utf-8")
shutil.copy2(ROOT / "dependencies.lock", OUT / "dependencies.lock")
symbols = OUT / "symbols" / version
symbols.mkdir(parents=True, exist_ok=True)
for name in ["ouse_core_desktop.elf", "ouse_core_desktop.map", "config/sdkconfig.h"]:
    shutil.copy2(BUILD / name, symbols / Path(name).name)
# Include ready-to-run USB installer, example, source and pyserial without pip.
subprocess.run([sys.executable,str(ROOT/"tools/bpk.py"),"pack",str(ROOT/"apps/counter"),str(OUT/"counter.bpk")],check=True)
shutil.copy2(ROOT/"tools/bpk.py",OUT/"bpk.py")
(OUT/"USB安装说明.md").write_text((ROOT/"docs/0.4.6-usb-bpk.md").read_text(encoding="utf-8").replace("ouse-core-0.4.6-full.bin",f"ouse-core-{version}-full.bin"),encoding="utf-8")
shutil.copy2(ROOT/"docs/0.4.7-wifi-compatibility.md",OUT/"WiFi修复说明.md")
shutil.copytree(ROOT/"apps/counter",OUT/"counter",dirs_exist_ok=True)
import serial
import importlib.metadata
assert serial.VERSION == "3.5"
shutil.copytree(Path(serial.__file__).parent,OUT/"serial",dirs_exist_ok=True,ignore=shutil.ignore_patterns("__pycache__","*.pyc"))
serial_dist=importlib.metadata.distribution("pyserial")
license_file=next((p for p in serial_dist.files if p.name=="LICENSE.txt"),None)
shutil.copy2(serial_dist.locate_file(license_file) if license_file else ROOT/"tools/fixtures/pyserial-LICENSE.txt",OUT/"pyserial-LICENSE.txt")
usb_files=["bpk.py","counter.bpk","USB安装说明.md","WiFi修复说明.md","pyserial-LICENSE.txt"]
if version == "0.4.8":
    shutil.copy2(ROOT/"docs/0.4.8-core1-diagnostics.md",OUT/"崩溃诊断说明.md")
    usb_files.append("崩溃诊断说明.md")
    readme=OUT/"README.md"
    text=readme.read_text(encoding="utf-8")
    heading,rest=text.split("\n",1)
    readme.write_text(heading+"\n\n**本包是 Core1 崩溃诊断版，根因尚未定位。串口保持 115200，异常后保留 OuseFault 与完整 UART core dump。只更新应用到 0x10000 可以保留设置和 BPK。**\n"+rest,encoding="utf-8")
usb_files += [str(p.relative_to(OUT)).replace("\\","/") for directory in ("serial","counter") for p in sorted((OUT/directory).rglob("*")) if p.is_file()]
with (OUT/"README.md").open("a",encoding="utf-8") as readme:
    readme.write("\n## USB 安装测试\n\n关闭串口监视器后，在本目录运行（替换 COM10）：\n\n```powershell\npython bpk.py --port COM10 hello\npython bpk.py --port COM10 install counter.bpk --open\npython bpk.py --port COM10 list\n```\n\n详见 USB安装说明.md。旧 Brookesia BPK 需要迁移，不能直接安装。\n")
with (OUT/"SHA256SUMS.txt").open("a",encoding="utf-8") as checksums:
    for name in usb_files: checksums.write(hashlib.sha256((OUT/name).read_bytes()).hexdigest()+"  "+name+"\n")
transfer_files = files + usb_files + ["flash.ps1", "README.md", "SHA256SUMS.txt", "build-info.json", "dependencies.lock", "图形工具烧录说明.txt"]
archive = OUT / f"ouse-core-{version}-firmware.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
    for name in transfer_files:
        z.write(OUT / name, name)
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
print(f"Packaged {description['project_name']} {version}: {archive}")
for offset, name in args["flash_files"].items():
    print(f"  {offset}: {name} ({(OUT/name).stat().st_size} bytes)")
