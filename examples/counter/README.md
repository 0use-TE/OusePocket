# 独立计数器应用

计数器使用官方 Toolkit `js-gui` 模板组织，采用 JSON UI 与 JavaScript，应用 ID 为 `net.0use.counter`。

完整流程见 [BPK 本地预览、打包与真机安装](../../docs/11-bpk-preview-and-install.md)。

本目录独立于 `brookesia-minimal` 固件，不参加固件资源 staging。先烧录默认桌面，之后才单独构建/安装本应用。见 [教程第七节](../../docs/04-brookesia-minimal-tutorial.md)。

## 安装现有包

接板子的 USB-UART（CH343）口，保持系统正常运行，先关闭串口监视器/助手。下面假设端口是 COM10，以实际端口为准。控制通道与串口日志共用115200波特率，与esptool烧录速度无关。

首次安装电脑端工具（Python 3.9及以上）：

```powershell
Set-Location D:\Projects\OusePocket
python -m pip install .\.reference\esp-brookesia\tools\brookesia_usb
```

查询连接并上传安装已生成的1.0.0包，无需构建固件：

```powershell
python -m brookesia_usb.cli --port COM10 --baudrate 115200 status
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\counter\dist\net.0use.counter.debug.1.0.0.bpk
```

`install` 自动完成上传、校验和安装，不必另行执行 `put`。成功后桌面出现 Counter / 计数器；打开后每次加1。

应用存放于内部 Flash 的 `/littlefs/apps/net.0use.counter/`，包含manifest、JS和UI资源。系统当场注册应用并刷新桌面，重启后通过扫描manifest恢复应用列表。计数值只存在内存；回桌面暂停会保留，停止应用或整机重启后归零。

验证首次安装后，可在最近任务中关闭计数器，再安装已生成的1.1.0包：

```powershell
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\counter\dist\net.0use.counter.debug.1.1.0.bpk
```

同ID替换应用，重新打开后每次加10；不重刷系统固件。重启一次可检查新版是否保留。完整重烧 `littlefs_data.bin` 会覆盖其中已安装的应用，日常应用更新使用上述安装命令。当前独立安装与更新尚待实机验证。

## 修改并重新打包

开发时先在 `tools/host-tools` 执行 `npm install` 安装 Toolkit（首次没有 lockfile），再在本目录执行：

```powershell
../../tools/host-tools/node_modules/.bin/brookesia.cmd build
node tools/set-version.mjs 1.1.0 10
../../tools/host-tools/node_modules/.bin/brookesia.cmd build
```

第二次构建产生同 ID 的 1.1.0 包，每次加 10。计数只保存在内存，关闭/更新后重新从 0 开始。
