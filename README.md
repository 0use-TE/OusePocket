# Ouse Core 0.28.0

OusePocket 是 ESP32-P4 上的轻量多媒体应用框架。仓库根目录就是 ESP-IDF 工程；硬件与系统服务使用原生 C/C++，桌面和设置使用 LVGL，独立应用使用 JSON＋JavaScript。旧 Brookesia 工程、微雪示例与历史资料保存在 `legacy/`，当前固件不依赖该目录。

当前目标板是微雪 ESP32-P4 WIFI6 Touch LCD 3.5，16 MB Flash、32 MB PSRAM、320×480 屏幕，要求芯片 revision ≥3.1。

## 本版状态

0.28.0 向 BPK 开放有权限与实例隔离的 BLE 客户端及系统 HID：扫描、连接、GATT 发现/读写/订阅，键盘、媒体键与鼠标报告。HID 仅前台应用可用，退出/失焦释放输入，持续按键有两秒保护。原生 API 31，JS API 1，不更新 C6。见 `docs/0.28.0-app-bluetooth-hid.md`。

0.27.0 将 BLE 加入设置，提供开关、限时搜索、配对确认和绑定保存，不更新 C6。修复录音机和相册授权后定时器调用错误，异常页直接显示错误及调用栈。原生 API 30，JS API 1；尚不开放 JS 蓝牙或 HID。见 `docs/0.27.0-bluetooth-runtime-errors.md`。

0.26.0 统一输入框聚焦光标，增加按应用 ID 保存的桌面顺序、长按拖拽插入及边缘停留跨页。新增仅调试使用的 BLE HCI 验证，不开放第三方蓝牙接口、不更新 C6 固件。原生 API 29，JS API 1。见 `docs/0.26.0-launcher-ime-ble.md`。

0.25.0 增加低内存分级准入和安全缓存回收、SD 无需重启重新检测及有限异常恢复，并修复主机 UI 测试夹具。原生 API 28，JS API 1。策略、统一回归入口及验证边界见 `docs/0.25.0-memory-sd-regression.md`。

0.24.0 增加唯一媒体流与提示流的 PCM 混合、平滑压低/恢复、持续提醒和实例隔离停止；倒计时更新为确认前持续响铃。兼容原音乐、闹钟、相册和录音应用，边界见 `docs/0.24.0-audio-mixing.md`。

0.23.0 增加动态应用目录和按需加载、麦克风/摄像头/共享删除授权、SD 安全卸载、有限 Wi-Fi 重试与时间同步状态，以及有界 WebSocket/MQTT 客户端和 IPv4 Ping。修复密码显隐、三键输入法避让、状态栏 Wi-Fi 图标及前台/音乐通知横幅。接口和限制见 `components/ouse_runtime/PORTING.md`。本轮不包含 OTA 和长期运行验收。

0.15.0 调整桌面为每页 12 个图标和分页圆点，补齐最近应用底部上滑返回、手势条跟手、快滑回桌面路径；区分计时器图标、服务测试分类，并在设置新增可动态开关及保存的 MCP 设备调试。C API 16，JS API 1。见 [交互与验证说明](docs/0.15.0-desktop-gestures.md)。

0.14.2 修复 USB 扬声器实际收不到 PCM 与后台页滚动回顶，见 [修复和验证](docs/0.14.2-usb-audio-scroll.md)。

0.14.1 整理 USB 离线禁用开关与后台服务卡片，见 [界面及 Windows 用法](docs/0.14.1-usb-service-ui.md)。

当前源码 **0.22.0**、C API **25**、JS API **1**。本轮新增 SD 录音机、相册连续滚动/删除/日期、Wi-Fi 信号与通知横幅，修复关闭音乐后仍播放，并实测 HTTPS 证书链兼容，见 [0.22.0 交付](docs/0.22.0-media-system.md)。新增共享摄像头系统 API、独立相机和 SD 相册应用，见 [摄像头与照片服务](docs/0.20.0-camera-gallery.md)。设置新增原生应用管理：应用大小、私有数据、打开、清除数据和卸载，界面精简见 [0.19.1 调整](docs/0.19.1-app-management-ui.md)，功能见 [应用管理](docs/0.19.0-app-management.md)。资源管理器侧边返回先关闭弹窗、逐级返回文件夹，到 SD 根目录后才退出，见 [返回行为](docs/0.18.2-explorer-back.md)。图标式连续滚动列表见 [连续滚动](docs/0.18.1-explorer-scroll.md)。整张 SD 卡的只读目录服务和 `sdcard` 权限见 [资源管理器](docs/0.18.0-sd-explorer.md)。受限文件服务、原生下载与 WAV 播放见 [文件与音乐说明](docs/0.17.0-sd-music.md)。底部手势隔离与无损 SD 卡识别见 [检测与验证边界](docs/0.16.1-sd-probe.md)。全面屏手势 / 三键导航见 [系统导航](docs/0.16.0-navigation.md)。USB 音频和网络共享见 [接口与验证边界](docs/0.14.0-usb-otg.md)。

上一轮 **0.13.1**、核心 C API **12**、JS API **1**。修正 JSON 行/列主轴 fill 分配、按钮子标签实时主题颜色继承、应用状态栏/导航栏背景及最近卡片顶部绘制边界。已构建、只烧录应用区并校验哈希，更新音乐、闹钟及计算器包。真机已确认音乐按钮布局与播放控制、明暗背景、计算器实时切换与重开一致、最近应用预览穿过半透明状态栏及上滑关闭。详见 [本轮修复与实机证据](docs/0.13.1-runtime-ui-fixes.md)；0.13.0 的应用与接口说明见 [应用交付](docs/0.13.0-shell-audio-apps.md)。

此前 **0.12.1** 已完成应用区烧录及相关真机验证，HTTPS 超时仍未定位；这份旧版本证据不能替代 0.13.0 手势、声音和应用的实机验收。见 [实机记录](docs/0.12.1-device-validation.md)。

上一版 **0.9.0**，公共核心 C API **8**、脚本运行时 API **1**。新增有界原生任务登记、实例所有权与终态事件、后台原因和诊断缓存；QuickJS 执行保护使用 FreeRTOS 任务获调度时间及独立经过时间熔断，不把经过时间称为 CPU 时间。详见 [本轮系统机制](docs/0.9.0-kernel.md)。本地统一验证和构建已通过；后经用户授权完成应用区烧录、哈希校验及简短启动通信确认。连接已恢复，完整真机功能验收仍待安排。

0.8.0 的紧凑面板、子页面、分类接口及五套真机验收属于历史版本，见 [0.8.0 记录](docs/0.8.0-navigation-ui.md)，不能作为当前版本硬件验收。依赖、分区、字体和显示等待修复保持。

默认固件只有设置应用。资源监视和接口测试已改为独立 BPK，计数器保留为开发示例。没有任何 JSON＋JS 应用再通过固件启动入口内置注册。

## 当前功能

- 原生桌面：顶部状态栏、应用图标和名称、底部手势条、桌面网格/分页。
- 原生系统交互：顶部左侧跟手下拉通知中心，右侧下拉快捷面板，展开后可横滑切换；左右边缘返回、上滑释放回桌面、上滑停住打开最近应用；卡片支持横向切换、恢复、上滑关闭、全部清除及点击空白退出。
- 快捷面板：紧凑时钟/日期/设置头部，Wi-Fi、电源、声音、主题四个独立快捷项；声音进入原生声音设置，主题直接切换明暗。下方仅有太阳图标和宽亮度滑条，不显示数值百分比；语言在设置中调整，字号倍率固定为默认值。
- 应用控件与通知：卡片、列表、列表项、工具栏、分割线、对话框和底部弹层；样式增量合并及主题/状态覆盖；通知发布、更新、取消、持续通知和点击恢复应用。
- 原生设置：首项显示 Ouse 本地账号与头像；保留亮度、主题、语言、电源、音量/静音和 Wi-Fi；底部“关于设备”显示设备名称、版本与硬件规格。设置立即生效、后台保存，失败时提示重试。
- Wi-Fi 设置：开关、当前网络、可用网络列表、信号/加密标记、扫描刷新、连接详情、手动添加与忘记网络；开放网络直接连接，不弹密码输入。
- BPK：应用自己的中英文名称、自定义透明图标和内部词表、包内图片/文本/二进制资源；支持串口安装、更新、列举、打开、卸载和重启恢复。
- 应用数据：异步私有 JSON 存储，关闭与更新保留、卸载清除；资源监视显示实际文件系统、安装包和应用数据统计。
- 应用子页面：一个 `page.json` 定义最多 16 页、8 层导航栈；`Ouse.navigation` 提供前进、返回、替换和页面变更通知。每次只建立当前页面，Home 保留导航栈，显式关闭后回到初始页。
- 共享主题与排版：浅深色语义配色、dp/sp、中英文即时切换、固定默认 100% 字号倍率；逻辑 DPI 由启动配置提供。共享 14/16/20px 中文 fallback，源字体覆盖约 5,000 常用汉字，详见 [字库说明](docs/0.4.13-fonts.md)。
- 硬件服务：AXP2101 电源/电池快照、背光与自动熄屏、ES8311 音量/静音/测试音/短 PCM 录放、P4 经 SDIO/C6 的 Wi-Fi 与 SNTP。测试录放放在接口测试 BPK，设置只保留日常控制。
- 本地电脑控制：CH343/UART0 的 OPK1 安装协议、控件树/JPEG/触摸注入和 stdio MCP 工具。板上没有网络 MCP 服务。

应用注册表按需扩容，没有固定的 24 个安装上限，安装与同时运行仍受 PSRAM、Flash 和共享服务预算约束。应用注册表、桌面/最近应用状态、BPK 内容、运行时模型及 QuickJS 堆使用 PSRAM；大块应用分配失败时不再回退挤占内部 RAM。迁移和离线验证见 [PSRAM 分配说明](docs/app-psram.md)。只有一个前台应用；回桌面默认保留实例并暂停脚本，声明后台能力且主动 begin 的应用可继续受限执行。关闭则释放 JS、控件、回调、定时器及预览。媒体回桌面可继续，显式关闭所属应用时会停止并释放；闹钟仍按服务自己的生命周期继续；当前媒体格式为 PCM16 WAV，闹钟不支持关机/深睡唤醒。音乐/闹钟 BPK 已提供；0.14.0 新增 USB OTG 音频和网络共享，GPIO 等其他硬件接口仍待后续。

主题的唯一颜色源为 `components/ouse_core/theme/colors.json`。修改后运行 `python tools/gen-theme-colors.py`，同步生成固件调色表、VS Code 预览 JSON 和扩展颜色常量，再构建相应产物。预览先校验并加载全部 12 个颜色 token，再首次绘制；默认应用背景使用 `background`。应用通过语义颜色和 `Ouse.theme` 接收当前明暗模式；这不是运行时从文件加载任意主题的接口。

“关于设备”显示板级规格，不显示资源使用量：ESP32-P4、32 MB PSRAM、16 MB Flash/microSD、3.5 英寸 ST7796 显示、FT6336 触摸、ES8311 音频、8 Ω / 2 W 扬声器、OV5647 500 万像素 MIPI-CSI 摄像头、C6 无线及 AXP2101。规格来自[归档微雪产品说明](legacy/ESP32-P4-WIFI6-Touch-LCD-3.5/README_ZH.md)，是板级静态声明，不是运行时外设探测；列出 Bluetooth LE 不等于本版已经开放蓝牙服务。实时堆、CPU 频率、运行时间和按核心负载估计由资源监视 BPK 查询；负载基于本核心已结算的 idle 累计值，不是精确 CPU 利用率或纯 JS CPU 时间。

## BPK 开发和安装

当前只有一种 BPK 格式。容器包含四个必备文件 `manifest.json`、`page.json`、`app.js`、`icon.bin`，以及清单 `assets` 声明的最多 16 项资源，使用 ZIP_STORED；清单不包含包格式版本字段。

清单提供默认 `name`、可选 `names.zh_CN/en` 和图标；语言缺项回退 `name`。桌面和最近应用读取包内名称并即时切换语言，不执行应用 JS。PNG 设计图在电脑上转为带尺寸头的 BGRA8888 `icon.bin`，板上直接显示，保留 alpha。应用内部通过 JSON `translations/textKey` 或 JS `Ouse.locale` 管理自己的文案，无需加入系统词表；没有 XML 页面解析器。

详见 [BPK 说明](docs/bpk.md) 和 [应用 API](apps/README.md)。已经生成的独立包：

| 应用 | 文件 | 字节数 |
| --- | --- | ---: |
| 资源监视 | `dist/bpk/resource-monitor.bpk` | 14,653 |
| 分类接口测试 | `dist/bpk/api-test.bpk` | 74,163 |
| 原生服务测试 | `dist/bpk/service-test.bpk` | 79,689 |
| Counter 示例 | `dist/bpk/counter.bpk` | — |
| Calculator 示例 | `dist/bpk/calculator.bpk` | 21,845 |

```powershell
python tools/bpk.py pack apps/resource-monitor dist/bpk/resource-monitor.bpk
python tools/bpk.py pack apps/api-test dist/bpk/api-test.bpk
python tools/bpk.py --port COM10 install dist/bpk/resource-monitor.bpk
python tools/bpk.py --port COM10 install dist/bpk/api-test.bpk
python tools/bpk.py --port COM10 list
```

安装需要 pyserial，只有从 PNG 转换图标时才需要 Pillow。仓库清单已经引用 `icon.bin`，常规打包不要求 Pillow。运行日志和安装串口速率为 921600，命令可用 `--baud` 改成与板子一致的速率；同一串口由一个工具独占，上传前释放串口监视器。

包上限 512 KiB；清单最多 8 KiB，页面/脚本各最多 32 KiB。图标宽高各 1～96，资源图片边长最多 512、像素总量 128 KiB；文本/二进制另有 128 KiB。安装先校验包与资源，再写临时文件并原子替换。LittleFS 的 `/ouse-apps` 存安装包，`/ouse-data/<id>` 存隔离 JSON 数据；应用卸载后清理其数据，其他已有文件保留。非空文件系统挂载失败时不自动格式化。

## 固定版本与构建

依赖清单和 `dependencies.lock` 固定版本，本版没有升级依赖，不保证任意 ESP-IDF 6.x 可直接替换。

QuickJS 组件单独加入 `-fno-strict-aliasing`，解决本机 GCC 15 RISC-V 优化下字符串迭代器索引不推进的问题；仍保留 `-O2`、QuickJS-NG 0.14.0 和原始 managed 源文件。修正前后的真机字符串对照与最终固件验收分别登记。

| 组件 | 版本 |
| --- | --- |
| ESP-IDF | 6.1.0，本机提交 `fff9895c82d744c7237be8847347bdd1b07c6643` |
| LVGL | 9.5.0 |
| 微雪 BSP | 2.0.2 |
| ESP LVGL Adapter | 0.6.4 |
| QuickJS-NG | 0.14.0 |
| ESP-Hosted / Wi-Fi Remote | 2.12.3 / 1.6.5 |
| LittleFS | 1.22.3 |

在 ESP-IDF 6.1.0 终端，从仓库根目录运行：

```powershell
idf.py build
```

应用产物为 `build/ouse_core_desktop.bin`，对应应用地址 `0x10000`。0.8.0 历史固件为 **7,470,448 字节**，8 MiB 应用分区剩 **918,160 字节**；已通过 COM10 / 921600 应用烧录与哈希校验，保留 NVS 和 LittleFS。五套检查与最终清理已完成，身份和证据见 [0.8.0 记录](docs/0.8.0-navigation-ui.md)。首次空白设备还需要 bootloader 与分区表，不额外生成合并固件包。

保留 CPU 400 MHz、PSRAM 200 MHz、XiP、双核和真实 LCD/Wi-Fi。LVGL 单绘制单元，UI 任务为 48 KiB 内部栈；所有 JS 由另一个 64 KiB 内部栈的任务执行，每实例有 512 KiB JS 堆、40 KiB JS 栈上限，前台/后台每轮任务获调度时间预算分别为 12/4 ms，详见本轮计时口径。显示 flush 等待继续使用已定位的让出 CPU 方案，等待真实完成而不提前复用 DMA 缓冲；该实现依赖固定 LVGL display 布局，升级时必须重新审视。

## 架构和后续工作

硬件工作任务不操作 LVGL，也不直接调用 JS。UI 读取缓存快照、提交请求；其他任务通过 `ouse_core_post_ui()` 排队进入 UI 锁。耗时 I/O、网络、录放和安装在各自工作任务执行。原生设置与共用控件不迁移到 QuickJS。

独立应用使用 JSON＋JS。应用自己的静态翻译即时更新，动态文字由脚本监听语言变化；挂起的 JS 通知合并到恢复时补发。图标描述符在注册期借用，更新/卸载先删除旧 UI，再在 UI 锁中清理图像缓存，最后由安装任务释放包。运行中的脚本拥有源码/页面资源副本，后台清理不再依赖安装包内存。

后续工作主要是中文拼音候选、CPU 占用率，以及按应用需求扩展的控件。音乐解码/流式播放、媒体会话、闹钟/倒计时调度与蓝牙仍待实现，详见 [当前待办](docs/STATUS.md)。

历史证据单独保留：[显示总线故障定位](docs/0.4.10-bus-timeout.md)、[0.4.11 验证](docs/0.4.11-system-verification.md)、[0.4.12 验证](docs/0.4.12-system-verification.md)。本版结果只按本次独立记录登记，不沿用旧版硬件结论。

## 目录

```text
main/                       启动和故障诊断，默认只注册设置
boards/waveshare_lcd35/      显示/触摸、板级规格和硬件任务
components/ouse_core/       原生桌面、设置、生命周期和共享排版
components/ouse_system/     设置保存与系统查询
components/ouse_devices/    电源/音频/Wi-Fi 状态
components/ouse_runtime/    JSON 页面和 QuickJS
components/ouse_storage/    原生私有存储任务、LittleFS 挂载与统计缓存
components/ouse_tasks/      有界任务登记、所有权、取消与完成状态
components/ouse_installer/  OPK1 和 BPK 管理
apps/                       独立资源监视/接口测试 BPK 源码
examples/                   计数器/计算器等 BPK 示例
dist/bpk/                   已打包应用
tools/                      打包、开发、串口控制与主机检查工具
docs/                       当前说明和历史故障/验证记录
legacy/                     旧 Brookesia 工程、微雪示例与历史文档
```

`tools/embed-script-fixtures.py` 只服务主机测试，不参与固件应用注册。桌面移植来源见 `components/ouse_core/BROOKESIA-NOTICE.md`，字体许可见 `components/ouse_core/FONT-LICENSE.txt`。
