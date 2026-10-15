# BPK 项目：本地预览、打包与真机安装

适用于当前 OusePocket：Windows + PowerShell、ESP32-P4、320×480 屏幕、JSON UI + JavaScript 应用。以已有计数器为例，也说明音乐和日志包的使用方式。

当前设备稳定基线暂时关闭USB/BPK接收服务，以下安装命令须在服务修复并恢复后使用。源码风险、恢复方案和验收步骤见 [USB服务恢复分析](13-usb-service-recovery.md)。本地应用开发与打包可先进行。

## 1. 先区分两个项目

| 内容 | 项目目录 | 产物 | 如何放到设备 |
| --- | --- | --- | --- |
| 系统固件：桌面、设置、HAL、共享服务 | `examples/brookesia-minimal` | `build/*.bin` 等 | ESP-IDF / esptool 烧录 |
| 独立应用：界面、JS、图片、Manifest | `examples/counter` 等 | `dist/*.bpk` | 系统运行时通过 Brookesia 安装 |

**BPK 通常叫“安装”，不按 Flash 地址烧录。** 桌面系统先烧进去并正常启动，后续改应用只重新打包、安装 BPK，不需要重新编译整个 ESP-IDF 项目，也不进入下载模式。

本地预览用 PC 上的 WASM 模拟器，不需要连接开发板；模拟器不会给设备写入数据。

## 2. 电脑环境准备

普通 PowerShell 即可进行 BPK 开发；只有构建、烧录系统固件时才需要 ESP-IDF 环境。

检查 Node.js、npm、Python：

```powershell
node --version
npm --version
python --version
```

官方 Toolkit 要求 Node.js 20 或更新版本。Python 用于安装上传工具，当前工具要求 Python 3.9 或更新版本。

本仓库 `tools/host-tools/package.json` 固定 Toolkit 为 `1.0.1`。为了先保持与项目一致，可全局安装同一版本：

```powershell
npm install -g esp-brookesia-toolkit@1.0.1
brookesia --help
brookesia simulate --help
```

如果提示找不到 `brookesia`，重新打开 PowerShell。此处无需安装 AI 插件或配置 MCP。

安装当前仓库配套的设备上传工具，只需一次：

```powershell
Set-Location D:\Projects\OusePocket
python -m pip install .\.reference\esp-brookesia\tools\brookesia_usb
python -m brookesia_usb.cli --help
```

也可以使用仓库本地 Toolkit，避免全局安装：

```powershell
Set-Location D:\Projects\OusePocket\tools\host-tools
# 当前目录没有 package-lock.json，首次使用 npm install。
npm install

Set-Location D:\Projects\OusePocket\examples\counter
& ..\..\tools\host-tools\node_modules\.bin\brookesia.cmd --help
```

下面命令都按全局 `brookesia` 写；使用本地方式时替换为上面这个 `.cmd` 路径。不要仅运行应用目录的 `npm run build` 却没有安装或提供 Toolkit。

## 3. 看懂一个 BPK 应用目录

```text
examples/counter/
├─ brookesia.config.js       Toolkit 输入/输出配置
├─ package.json             npm 命令入口
├─ src/
│  ├─ manifest.json         应用 ID、版本、运行时、共享服务依赖
│  ├─ app/main.js           应用逻辑和生命周期
│  └─ res/
│     ├─ profile.json       UI 根资源、启动图标、屏幕流挂载层
│     ├─ root.json          引入图片、屏幕、屏幕流等资产
│     ├─ images/            应用图标和图片
│     ├─ screens/main.json  界面节点和交互动作
│     └─ flows/main.json    页面流
├─ build/                   Toolkit 中间产物
└─ dist/                    可安装的 BPK
```

`manifest.json` 的 `package.id` 是系统识别应用的身份，更新时保持不变；改 `package.version` 表示新版。`package.json` 的 npm 包名不代替 Manifest 中的应用 ID。

JSON 描述 UI，JavaScript 响应动作并调用系统服务；BPK 不包含本项目的 ES8311、SDMMC 等 C++ 驱动。需要新硬件能力时，先在固件提供服务，再由应用声明和调用。

## 4. 本地预览已有计数器

```powershell
Set-Location D:\Projects\OusePocket\examples\counter
brookesia doctor
brookesia build
Get-ChildItem .\dist\*.bpk
brookesia simulate --resolution 320x480
```

`build` 生成开发用 debug BPK；`simulate` 默认加载 `dist` 中最新的包并启动本地 HTTP 服务，通常自动打开浏览器。以终端实际输出的网址为准，默认 HTTP 端口为 `8787`。

也可以明确指定某个已有包，避免新版/旧版混淆：

```powershell
brookesia simulate --package .\dist\net.0use.counter.debug.1.0.0.bpk --resolution 320x480
```

预览时检查：启动能否完成、点击是否计数、文字有无裁切、按钮是否可点击、图片是否透明且比例正确。

### 修改后如何刷新

先使用明确、可重复的流程：

1. 修改 `src/res/screens/main.json` 或 `src/app/main.js`。
2. 在模拟器终端按 `Ctrl+C` 停止服务。
3. 再执行 `brookesia build`。
4. 再执行 `brookesia simulate --resolution 320x480`。

不要假定修改 `src` 后浏览器必然实时更新；当前教程不依赖自动热重载。系统中同 ID BPK 的更新安装是另一件事。

浏览器未自动打开时：

```powershell
brookesia simulate --resolution 320x480 --no-open --port 8788
```

手动打开终端给出的地址。预览和 USB 串口的波特率没有关系。

### 预览的范围

官方 WASM 模拟器可以运行 JSON UI 应用，但不是当前产品固件的完整复制。适合检查界面和模拟器支持的交互。

OusePocket 自定义的 `Pocket` 服务不一定存在于官方模拟器。音乐、日志应用调用此服务，直接预览可能报服务不存在；这不等于真机安装包有问题。要完整预览这些应用逻辑，需要为模拟器补充同名模拟服务；当前项目尚未提供该适配，不能用浏览器证明 SD 卡、扬声器、日志持久化正常。

系统桌面的原生最近任务、手势、显示驱动及设置扩展也需要真机验证。仓库的 `test-shell-controls.py` 等是开发回归夹具，不是可以完整操作的系统模拟器。

## 5. 新建自己的应用

```powershell
Set-Location D:\Projects\OusePocket\examples
brookesia init my-app --template js-gui
Set-Location .\my-app
brookesia doctor
brookesia build
brookesia simulate --resolution 320x480
```

按命令输出确认实际创建目录。`js-gui` 模板没有额外应用 npm 依赖；`js-bundle` 模板则需要在应用目录执行 `npm install`。

先修改 Manifest 中的应用 ID、名称、版本，再编辑 JSON/JS。共享服务必须在 Manifest 声明，例如当前产品的 Pocket：

```json
"services": [
  { "name": "Pocket", "version": "1.0.0" }
]
```

上面是 Manifest 的一个字段，不是完整文件。内置 SystemGui 等系统接口与产品自定义服务不同，参考现有应用声明，不要凭名字猜服务参数。

当前 GUI 资源注意点：图片资源写为 `${image.<id>}`，字体尺寸用 `sp`，布局尺寸用 `dp`，布局中的子节点明确指定 `placement.mode: "flow"`。每个文档的事件动作名需唯一。JS 入口可用 IIFE 包裹局部变量，避免入口重新求值时发生顶层 `let/const` 重复声明；参见音乐与日志应用的写法。

## 6. 把 BPK 安装到设备

前提：设备已经正常进入桌面，固件 USB/UART 安装服务正常工作。系统若仍在启动崩溃循环，先修复固件，安装工具无法绕过它。

1. 接设备的 CH343 USB-UART 接口。
2. 在设备管理器确认 COM 端口，下面假设 `COM10`。
3. **关闭 VS Code 串口监视器、串口助手**，释放端口。
4. 保持正常启动模式，不按住 BOOT 进入下载模式。

先查询，再安装：

```powershell
Set-Location D:\Projects\OusePocket
python -m brookesia_usb.cli devices
python -m brookesia_usb.cli --port COM10 --baudrate 115200 status
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\counter\dist\net.0use.counter.debug.1.0.0.bpk
```

COM 端口按实际替换，包名按 `dist` 中实际文件替换。

`install` 一次完成上传、传输校验及系统安装，不需要先手动 `put`，也不需要指定 `0x...` 地址。打开串口可能使板子复位，CLI 会等待服务；若最终超时，检查桌面能否正常启动。

当前 UART 控制通道与固件日志共用 **115200**。VS Code 的 `idf.flashBaudRate=921600` 只决定固件烧录速度，不能因此把 BPK 安装命令改成 921600。

安装成功后，在桌面找到计数器并打开。包位于内部 Flash 文件系统 `/littlefs/apps/net.0use.counter/`，系统注册应用，重启后扫描 Manifest 恢复列表。保存应用不代表应用状态自动持久化；当前计数值只存在内存。

## 7. 修改应用后更新安装

计数器已有版本演示脚本，将版本改为 `1.1.0`，步长改为 `10`：

```powershell
Set-Location D:\Projects\OusePocket\examples\counter
node .\tools\set-version.mjs 1.1.0 10
brookesia build
brookesia simulate --package .\dist\net.0use.counter.debug.1.1.0.bpk --resolution 320x480
```

预览正常后停止模拟器，在设备最近任务中关闭旧计数器，然后安装：

```powershell
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\dist\net.0use.counter.debug.1.1.0.bpk
```

保留 `package.id`，更新 `package.version`；重新打开应每次加 10。再重启设备，检查应用仍在且版本正确。这是独立更新应用包，不是保证正在执行的旧 JS 状态无缝迁移。

开发自用先使用 debug 包即可。签名 release 是另一条流程：`brookesia sign init`、`brookesia release`、`brookesia verify`，需要与固件的签名验证策略配套；签名私钥不要打进 BPK 或提交到 Git。

## 8. 安装当前音乐和日志应用

两个独立应用已有轻量 Python 打包脚本，不需要先运行 Toolkit 的构建流程：

```powershell
Set-Location D:\Projects\OusePocket
python .\tools\package-runtime-apps.py

python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\music\dist\net.0use.music-1.0.0.bpk
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\log-viewer\dist\net.0use.logs-1.0.0.bpk
```

这条脚本输出的是当前开发固件接受的未签名开发包，不代替 Toolkit 的所有检查或 release 签名。音乐/日志目录目前没有 Toolkit 项目配置，不能直接照计数器执行 `npm run simulate`；可用官方 `simulate --package <绝对BPK路径>` 加载包，但完整功能仍受 Pocket 模拟服务缺失的限制。

音乐放入 SD 卡 `/music` 目录。更多操作见[音乐说明](../examples/music/README.md)与[日志说明](../examples/log-viewer/README.md)。两者需要固件包含相应原生服务；仅安装 BPK 不会给旧固件补上 MP3 解码器或日志后端。

## 9. 什么时候才烧录系统固件

修改 C++、HAL、系统共享服务、sdkconfig、内置桌面或 Settings 资源时，才进入系统项目构建烧录。

在 **VS Code ESP-IDF 扩展终端**执行：

```powershell
Set-Location D:\Projects\OusePocket\examples\brookesia-minimal
idf.py build
idf.py -p COM10 -b 921600 flash
idf.py -p COM10 monitor
```

此命令会按 `flasher_args.json` 烧录项目定义的镜像，包括 LittleFS。**重烧 `littlefs_data.bin` 会替换里面的独立应用、日志和用户文件，先备份。**

如果只修改主程序，分区、系统资源和文件系统内容都未变，可使用：

```powershell
idf.py -p COM10 -b 921600 app-flash
```

这只更新应用固件分区，不更新桌面 JSON、图片等 LittleFS 资源。改过资源却只执行 `app-flash`，会出现新程序配旧资源的问题。

按用户当前工作流，构建产物保留在 `examples/brookesia-minimal/build`，**无需复制到 `build/bootloader/bin`**。日常 BPK 更新更不需要执行固件 `flash`。

## 10. 常见问题

| 问题 | 检查方法 |
| --- | --- |
| `brookesia` 找不到 | 确认 Toolkit 安装完成，重开终端，或使用本地 `.cmd` 的完整路径 |
| 模拟器缺少 WASM 产物 | 执行 `brookesia doctor`；官方模拟器包随 Toolkit 安装，不能只拷贝 CLI 文件 |
| 浏览器仍是旧版本 | 停止模拟器，重新 build，明确指定新的 BPK，再启动 |
| 端口 `PermissionError` / 拒绝访问 | 关闭占用同一 COM 的监视器或助手，确认设备管理器端口 |
| `hello` / `status` 超时 | 检查 COM、115200、CH343接口、固件是否稳定进入桌面，以及安装服务是否启用 |
| `service is not declared in app manifest` | 在 Manifest 声明应用需要的产品服务，再重新打包 |
| 服务不存在 | 确认固件具备该服务；官方模拟器没有产品 Pocket 服务也会失败 |
| `redeclaration of gui` | 避免重复定义顶层变量；参照现有新应用，用局部作用域包装入口 |
| 改 JSON 后启动失败 | 检查动作是否重复、图片引用、字体 sp 单位，以及节点布局模式 |
| 固件烧录后应用消失 | 检查是否重烧了 LittleFS；重新安装 BPK |

## 11. 本教程的验证范围与来源

教程按仓库现有 Counter 配置、USB CLI 源码与官方 Toolkit 文档整理。当前环境尚未实际启动 Toolkit 浏览器模拟器，也未将这些命令用于本轮实机安装；不要把文档检查当作预览或设备验收结果。

- [官方 Toolkit：创建、构建、模拟、部署](https://docs.espressif.com/projects/esp-brookesia/en/latest/system/toolkit.html)
- [官方串口安装工具](https://docs.espressif.com/projects/esp-brookesia/en/latest/system/usb_cli.html)
- 仓库对应文档：`.reference/esp-brookesia/docs/zh_CN/system/toolkit.rst`、`.reference/esp-brookesia/tools/brookesia_usb/README_CN.md`。
