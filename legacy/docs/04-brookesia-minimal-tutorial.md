# 从 ESP-IDF 到 Brookesia：桌面、设置与可更新计数器

本篇只做一个小系统：开机进入桌面，打开 Settings 调亮度，再安装计数器；把计数器从“每次加 1”更新成“每次加 10”，无需重新烧录固件。

**按你最新决定，第一次只完成默认桌面烧录，计数器不预装、不编进固件。** 当前直接看第三节的源码准备和第六节的构建/烧录即可；电脑模拟、计数器安装和热更新都留到以后。默认桌面的 Settings 是框架自带入口，也可以暂时不操作。

核对日期：2026-10-14。使用当日最新 Brookesia master 源码快照 `e937455b0db1a3e873b1da61d6d13652f3dcc7e2`（0.8 系列组件），不是微雪示例 11 的旧 Phone。为让教程可复现，准备脚本固定到这个提交；“采用最新”不意味着每次构建都自动换 API。ESP-IDF 采用本机已有的 6.1，LVGL 由新 GUI 组件固定到 9.5.0，微雪 BSP 固定 Registry 2.0.2。

## 一、先回答你的六个问题

### Brookesia 到底起什么作用？

你的理解基本正确：ESP-IDF 中可以创建任务，由 FreeRTOS 调度，驱动屏幕、音频、摄像头和网络。业务也可以写在事件回调、定时器或普通函数里，不一定每个功能都要单独一个任务。

Brookesia 在这些基础上帮你组织“一个带应用的产品”：桌面怎么启动应用，应用怎么退出，多个应用怎样使用同一套服务，界面资源怎样加载，动态应用包怎样安装。它自己也使用任务和驱动。

用你熟悉的 .NET 思路类比：ESP-IDF 提供底层能力；Brookesia 提供宿主、模块生命周期、服务注册和调用、界面管理；你写自己的功能模块。这个类比用于理解组织方式，不表示它在 MCU 上运行 .NET。

### 默认有桌面吗？

**有现成的桌面实现，但要选用并启动 `brookesia_system_super`。**

`System Core` 管应用、GUI、运行时和服务；`System Super` 在它上面提供面向触摸设备的系统壳，包括桌面背景、状态栏、应用启动器、系统覆盖层等。只引入一个服务组件，不会自动得到桌面。

本教程复用 System Super；你不用先从零画所有桌面控件。桌面需要的字体、图片和 JSON 资源也必须进入文件系统。

### Settings 软件有吗？

**有：`brookesia_app_settings`。** 它是原生 C++ 内置应用，附带界面资源，和 System Core 的应用生命周期对接。

有设置界面不等于所有硬件功能自动可用。亮度需要 Display 服务和背光 HAL；音量需要音频能力；配网需要 Wi-Fi 能力。缺少的服务/设备不会因为菜单存在就出现。

本例只接屏幕、触摸、背光与存储。Settings 里的亮度、主题/语言等可作为学习入口；不把其他页面当成已经实现的产品功能。

### 还要桥接各种外设吗？

**要，但不用重新写微雪已经提供的底层驱动。**

本板没有当前 Brookesia 自带的完整板级配置，所以增加一层小适配器：

| Brookesia 接口 | 本例连接到微雪/ESP-IDF 的函数 |
| --- | --- |
| `PanelIface::draw_bitmap()` | `esp_lcd_panel_draw_bitmap()`，句柄由 `bsp_display_new()` 提供 |
| `PanelIface::get_driver_specific()` | 提供 SPI panel/io 句柄和传输完成事件桥 |
| `TouchIface::read_points()` | `bsp_touch_new()` 初始化，再读取触摸点 |
| `BacklightIface::set_brightness()` | `bsp_display_brightness_set()` |
| 存储 HAL | 复用 Brookesia HAL adaptor 的 LittleFS 与 NVS 实现 |

以后音频、SD、Wi-Fi 也遵循同样思路。本篇先不接它们，避免理解时一次跨太多层。

### UI 怎么开发，LVGL 要自己桥接吗？

本例 UI 使用 **JSON 描述控件，JavaScript 处理业务**。Brookesia GUI 后端把 JSON 转成 LVGL 控件，所以计数器代码不用直接调用 `lv_label_create()`。

你要把板的显示/触摸提供给 HAL，再由 `gui::lvgl::DisplaySource` 启动 LVGL 和显示通道，选择 `gui::lvgl::Backend` 渲染界面。本例**不调用 `bsp_display_start()`**，否则微雪和 Brookesia 都可能启动一套 LVGL。

关系只有这一条：

```text
JSON UI + JavaScript
        ↓ SystemGui
Brookesia GUI / LVGL Backend
        ↓ Display 服务 / HAL
微雪 BSP / ESP-IDF 驱动
        ↓
320×480 屏幕、触摸、背光
```

### 有应用隔离和共享系统服务吗？

**共享服务有；隔离需要区分应用类型。**

| 项目 | 当前源码能提供的边界 |
| --- | --- |
| 原生 C++ 应用 | 编进同一个固件，共享地址空间，作为可信代码使用；内存错误可能影响整机 |
| 本例 JS 应用 | 每个应用创建自己的 QuickJS runtime/context，业务变量分开；源码设置 JS 内存与栈上限 |
| JS 的 GUI/定时器 | HostBridge 关联调用者，只能操作自己的 GUI 文档和自己的计时器 |
| JS 调其他服务 | manifest 的服务声明参与访问检查；`SystemCore`、`SystemGui`、`SystemTimer` 有隐式允许规则 |
| 硬隔离/恶意代码容错 | 不能当成 PC 操作系统的独立进程或完整安全沙箱；死循环、宿主错误和总内存不足仍可能影响系统 |

音频、存储、显示等可以注册为共享服务。应用通过服务名、函数名和参数调用，还能订阅事件。显示/音频等具体仲裁由相应服务实现，不是所有 GPIO 自动获得互斥保护。

本例计数器只有 `SystemGui`，所以 manifest 不声明额外硬件服务。你可以把它理解为只拿到“操作自己界面”的入口。

以上能力对应 [System Super](https://docs.espressif.com/projects/esp-brookesia/en/latest/system/super/index.html)、[Settings](https://github.com/espressif/esp-brookesia/tree/e937455b0db1a3e873b1da61d6d13652f3dcc7e2/app/brookesia_app_settings)、[System 服务边界](https://docs.espressif.com/projects/esp-brookesia/en/latest/system/core/services.html)。具体实现以本教程固定提交为准。

## 二、先看文件，不急着烧录

```text
OusePocket/
  tools/prepare-brookesia.ps1        下载/检查固定 Brookesia 源码
  tools/host-tools/package.json     官方应用工具链
  .reference/esp-brookesia/          下载的源码，Git 忽略
  examples/brookesia-minimal/       设备固件
    main/main.cpp                  启动服务、LVGL、系统桌面
    main/waveshare_display.cpp      微雪显示/触摸/背光适配
    main/idf_component.yml          构建依赖
    sdkconfig.defaults             最小功能配置
    partitions.csv                 固件与 LittleFS 分区
  examples/counter/                可以单独更新的 JS 应用
    src/manifest.json              应用 ID、名称、版本、运行时
    src/app/main.js                计数逻辑
    src/res/screens/main.json      控件
    src/res/flows/main.json         首屏流程
    src/res/profile.json            UI 入口与启动流程
    src/res/root.json               界面资源列表
    tools/set-version.mjs           修改版本与步长
```

你只需要先看两个文件：`main/main.cpp` 和 `counter/src/app/main.js`。板级适配的 DMA/回调细节先当成已准备的底座，等桌面和计数器跑通再读。

## 三、准备工具与最新源码快照

以下使用 Windows PowerShell，在仓库根目录执行。首次桌面烧录需要 Git 和可正常运行的 ESP-IDF 6.1 环境；以后开发计数器才需要 Node.js 20+ / npm。

```powershell
Set-Location D:\Projects\OusePocket
./tools/prepare-brookesia.ps1

# 以下应用工具可等到开发计数器时再安装；首次桌面烧录不需要。
npm.cmd install --prefix tools/host-tools
./tools/host-tools/node_modules/.bin/brookesia.cmd --help
```

已经下载的 reference 会检查提交和未提交修改，脚本不会擅自覆盖你修改过的框架。`override_path` 是这个学习工程读取固定最新源码的方法；微雪 BSP 仍消费公开 Registry 包。正式产品如何发布依赖后续再定。

应用工具链固定 `esp-brookesia-toolkit 1.0.1`。工具链 npm 版本与固件组件 0.8.x 是两套版本号，不能混为一谈。官方指令说明见 [Toolkit](https://docs.espressif.com/projects/esp-brookesia/en/latest/system/toolkit.html)；本轮还实际执行过安装、模板生成和打包，以工具输出为准。

## 四、先在电脑上运行计数器

这一步不需要 P4、C6 版本，也不会烧录硬件。

```powershell
Set-Location D:\Projects\OusePocket\examples\counter
../../tools/host-tools/node_modules/.bin/brookesia.cmd build

# 把模拟器数据留在仓库的工具目录，便于与个人数据分开。
$env:BROOKESIA_HOME = 'D:/Projects/OusePocket/.tools/state'
../../tools/host-tools/node_modules/.bin/brookesia.cmd simulate --resolution 320x480
```

终端会输出完整的 `Open:` 地址。若浏览器未自动打开，复制**完整地址**到 Chrome；只打开端口根地址可能丢失应用包参数。关闭模拟器用该终端的 Ctrl+C。

期望结果：显示 Counter；点击 Add，数字依次变成 1、2、3；点击 Reset 回到 0。模拟器自带的 Settings/Files/App Store 不表示最小设备固件也包含全部应用。

## 五、读懂计数器的 UI 和代码

### 1. manifest：告诉宿主“我是什么应用”

实际文件 `examples/counter/src/manifest.json` 的关键结构：

```json
{
  "package": {
    "id": "net.0use.counter",
    "name": { "en": "Counter", "zh_CN": "计数器" },
    "version": "1.0.0",
    "visible": true,
    "systems": ["super"]
  },
  "runtime": {
    "type": "JavaScript",
    "entry": "app/main.js",
    "resource_dir": "res",
    "arguments": []
  }
}
```

更新时 **ID 保持不变**，版本号改变。ID 变了就会被当成另一个应用。

### 2. JSON UI：定义控件和事件

计数标签在 `/main/value`；Add 按钮发布 `counter.increment` 动作。完整界面已经放在 `src/res/screens/main.json`，其中一个按钮如下：

```json
{
  "type": "button",
  "id": "increment",
  "layout": { "type": "flex", "mainAlign": "center", "crossAlign": "center" },
  "placement": { "width": "200dp", "height": "56dp" },
  "events": [{ "type": "clicked", "action": "counter.increment" }],
  "children": [{
    "type": "label",
    "id": "text",
    "labelProps": { "text": "Add" },
    "commonProps": { "clickable": false }
  }]
}
```

它不是网页 DOM，也没有浏览器 CSS；JSON 控件、布局、事件由 Brookesia GUI 解析，再交给 LVGL。第一课先使用英文按钮，减少字体与国际化细节。

### 3. JavaScript：只写计数和界面更新

`src/app/main.js` 的核心代码：

```javascript
const STEP = 1;
const VERSION = '1.0.0';
let count = 0;
let gui = null;

function callGui(name, params) {
  return brookesia.call_function(gui, name, JSON.stringify(params));
}

function render() {
  callGui('SetText', { Path: '/main/value', Text: String(count) });
  callGui('SetText', {
    Path: '/main/version', Text: 'v' + VERSION + ' | step ' + STEP
  });
}

globalThis.brookesia_app = {
  on_start() {
    count = 0;
    gui = brookesia.start_service('SystemGui');
    callGui('SubscribeAction', { Action: 'counter.increment' });
    callGui('SubscribeAction', { Action: 'counter.reset' });
    render();
    return true;
  },
  on_action(action) {
    if (action === 'counter.increment') count += STEP;
    else if (action === 'counter.reset') count = 0;
    else return true;
    render();
    return true;
  },
  on_stop() {
    if (gui !== null) brookesia.stop_service(gui);
    gui = null;
    return true;
  }
};
```

`on_start` 是打开应用的入口；`on_action` 接按钮事件；`on_stop` 释放服务句柄。没有创建自己的 FreeRTOS 任务，也没有直接操作显示驱动；框架负责把事件送到应用，再通过 `SystemGui` 更新它自己的标签。`SetText` 被接受不代表屏幕已同步完成绘制。

计数暂时只在内存中；关闭/重开/更新后回到 0。以后再用 Storage 保存，这一课先不加入持久化。

## 六、把桌面与 Settings 烧到微雪板

### 1. 先确认工具链和 P4 revision

在 ESP-IDF 终端执行。如果沿用本机路径，可以这样激活：

```powershell
$env:IDF_TOOLS_PATH = 'D:/Espressif'
$env:PYTHONUTF8 = '1'
. D:/esp/v6.1/esp-idf/export.ps1
idf.py --version
python -m esptool --port COM5 chip-id
```

把 `COM5` 替换成设备 USB-UART 的端口。进入下载模式时按板卡说明操作 BOOT/RST。读取芯片信息后再选择 profile：rev1.x 使用 `rev1_3`，rev3.x 使用 `rev3_x`，别把 PCB 版本当成芯片 revision。若工具报告其他 revision，先核对 IDF 支持范围。

最小示例暂不使用 C6，所以 C6 固件版本不会阻碍这一课。

### 2. 构建

下面两组选择一组执行。独立构建目录与配置文件避免两个 revision 的配置混用。

```powershell
Set-Location D:\Projects\OusePocket\examples\brookesia-minimal

# 仅在检测确认为 rev3.x 时：
idf.py -B build-rev3 -D SDKCONFIG=sdkconfig.rev3 -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev3_x' set-target esp32p4
idf.py -B build-rev3 -D SDKCONFIG=sdkconfig.rev3 build
```

```powershell
# 仅在检测确认为 rev1.x 时：
idf.py -B build-rev1 -D SDKCONFIG=sdkconfig.rev1 -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_3' set-target esp32p4
idf.py -B build-rev1 -D SDKCONFIG=sdkconfig.rev1 build
```

`SDKCONFIG_DEFAULTS` 只提供初始默认值，已有配置不会自动随 defaults 改动。构建后检查对应 sdkconfig 中 `ESP32P4_SELECTS_REV_LESS_V3` / `ESP32P4_REV_MIN_*`；改变 profile 时用另一组目录/配置，而不是复用旧配置。

本例是教学固件，`partitions.csv` 只有一个固件槽和 LittleFS，**没有整机 OTA**。第一次完整烧录会覆盖对应固件/分区布局及出厂资源；若板上已有要保留的数据，先备份再执行。

```powershell
# 与上一步选择的目录匹配，这里以 rev3.x 为例：
idf.py -B build-rev3 -D SDKCONFIG=sdkconfig.rev3 -p COM5 flash monitor
```

显示 `READY: System Super + Settings + BPK installer` 后，期望看到桌面和 Settings 图标。第一次完成到这里就停，计数器以后单独安装，不必继续第七节。若顺便测试设置，可以打开 Settings 找到显示/亮度设置，拖动后实际背光应变化；底边上滑返回桌面。

亮度适配器暂时把非零亮度限制在 10–100%，避免学习时调到看不见；显式灭屏接口仍可关背光。触摸方向、亮度保存和 320×480 下 Settings 布局需要你的实机检查。

### 3. 固件主流程为什么只有这么几步？

配套 `main/main.cpp` 使用的真实接口如下，片段用于读代码，完整可编译入口见文件：

```cpp
auto &manager = service::ServiceManager::get_instance();
manager.init();
manager.start();

// 启动共享 Display 服务，持有 binding 防止它被释放。
auto display_binding = manager.bind(DisplayHelper::get_name().data());
gui::lvgl::DisplaySource::get_instance().start({});

// 实际文件还激活 gui source role，并开启底边手势。
auto shell = std::make_unique<system::super::System>();
system::super::System::Config config;
config.core_config.gui_backend = std::make_unique<gui::lvgl::Backend>();
config.core_config.environment = {
    .width_px = 320, .height_px = 480, .density = 1.0F, .font_scale = 1.0F,
};
shell->init(std::move(config));
shell->start();
```

完整文件检查各步返回值，并让系统对象和 Display binding 持续存在。Settings 组件会注册应用 provider，构建脚本把它的资源与桌面资源打包到 LittleFS，系统负责安装/显示图标。

板级文件实现的是前面表中的 HAL 接口，不重复启动 LVGL。SPI 屏传输是异步的，因此它还把“颜色传输完成”事件送回 Brookesia/LVGL；漏掉这一步可能第一帧之后就不再刷新。

## 七、安装计数器，再热更新

此处“热更新”指**只更新 JS/JSON 应用包，不重新编译烧录系统固件**。不承诺旧应用在执行中无缝换代码或自动迁移内存状态。

先按 Ctrl+] 退出 `idf.py monitor`，同一串口不能同时被监视器和安装工具读取。安装本教程同一提交里的官方串口工具，避免工具与设备协议错配：

```powershell
Set-Location D:\Projects\OusePocket
python -m pip install -e .reference/esp-brookesia/tools/brookesia_usb
brookesia-usb --port COM5 --baudrate 115200 status
```

这里走板上的 **USB-UART 口 → UART0**，不是高速 OTG 口。固件的 console 和 Brookesia host transport 已匹配为 UART0 / 115200。

先确保构建的是 1.0.0：

```powershell
Set-Location D:\Projects\OusePocket\examples\counter
node tools/set-version.mjs 1.0.0 1
../../tools/host-tools/node_modules/.bin/brookesia.cmd build
brookesia-usb --port COM5 --baudrate 115200 install ./dist/net.0use.counter.debug.1.0.0.bpk
```

期望：桌面出现 Counter 图标，打开后版本为 1.0.0，Add 每次加 1。

更新：先返回桌面关闭计数器，然后执行：

```powershell
node tools/set-version.mjs 1.1.0 10
../../tools/host-tools/node_modules/.bin/brookesia.cmd build
brookesia-usb --port COM5 --baudrate 115200 install ./dist/net.0use.counter.debug.1.1.0.bpk
```

再打开 Counter：版本为 1.1.0，Add 每次加 10，初始计数为 0。两次包 ID 相同，System Core 的安装入口允许替换已有包。更新之后重启一次，确认仍是新版且桌面可启动。

**不要用 `idf.py flash` 更新计数器。** 完整 flash 会重新写出厂 LittleFS，可能覆盖动态安装内容。日常只运行包安装命令。

USB-UART 打开串口可能因 DTR/RTS 复位板子，因此这里保证的是“不重刷固件的应用更新”，不能保证串口安装时整机完全不重启。工具会等待设备就绪；若返回失败，先检查状态和日志，避免盲目重复安装。固件运行期的安装机制见 [USB CLI](https://github.com/espressif/esp-brookesia/blob/e937455b0db1a3e873b1da61d6d13652f3dcc7e2/tools/brookesia_usb/README.md)。

本例使用 debug 包，适合你自用可信应用的学习场景。USB 链路的 SHA-256 校验用于传输完整性；它不能证明发布者身份。以后从云端分发再增加签名信任策略，本课不引入审核后台。

## 八、遇到问题时按层排查

| 现象 | 先检查 |
| --- | --- |
| 依赖找不到 | 先运行 prepare 脚本，确认 reference 提交与 BSP 2.0.2 下载；不要克隆旧 Phone 到新工程 |
| 工具链激活失败 | 先让 ESP-IDF 的 `idf.py --version` 正常，再构建；本例不负责修复全局安装 |
| 黑屏/第一帧不刷新 | BSP 初始化、SPI IO 完成事件、DisplaySource、GUI source role 与 LittleFS 是否烧录 |
| 有显示但触摸不响应 | 触摸 HAL 读取、方向/镜像；本例使用 polling，不依赖中断模式 |
| 桌面资源找不到 | 构建日志是否生成 LittleFS 图像，字体/Settings/桌面 stage target 是否完成 |
| Settings 亮度不生效 | BacklightIface、Display 输出绑定与 BSP 背光函数；不能只检查滑块 |
| 安装连不上 | 正确 USB-UART 端口、115200、关闭 monitor、启动日志是否 READY |
| 新包没有替换旧应用 | package ID 相同、版本与步长正确、install 返回成功；重开应用查看版本 |
| 设置里的 Wi-Fi/音量不可用 | 本例未接这些外设；下一课再添加对应 HAL/服务 |

## 九、本轮验证记录

验证证据随实现补充在这里；编译、模拟运行、实物运行分别记录。

- 官方 Toolkit 1.0.1 生成模板，并成功打包 Counter 1.0.0 / 1.1.0。
- WASM 模拟器在 320×480 下成功加载 1.0.0，点击 Add 后从 0 变成 1。
- 桌面固件已在 ESP-IDF 6.1 下成功编译、链接并通过分区大小检查：固件 `0x660420` 字节，8MiB 固件分区剩余约 20%；LittleFS 图像已生成。
- 本轮构建使用 IDF 默认的 rev3.1 最低版本配置，证明源码能编译；没有验证 rev1 profile。上板时务必按实物 revision 选第六节的 profile 重新构建，不直接使用未知 revision 的现成二进制。
- 已检查出厂 LittleFS 的 apps 目录：只有框架 Settings 资源，**没有 `net.0use.counter`**。计数器不参加固件构建/staging。
- 新版 1.1.0 已完成打包校验；新版模拟交互因浏览器连接中断未完成，不能当成已验证的串口热更新。
- 没有烧录你的板，没有验证实物触摸/亮度/重启持久性/串口安装；这些按第六、七节操作后再记录结果。

当前先完成第六节，看到默认桌面就停。之后再读第四、五、七节，理解计数器的 UI、业务逻辑与独立安装。
