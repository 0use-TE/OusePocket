# Ouse Core 0.8.0

OusePocket 是 ESP32-P4 上的轻量多媒体应用框架。仓库根目录就是 ESP-IDF 工程；硬件与系统服务使用原生 C/C++，桌面和设置使用 LVGL，独立应用使用 JSON＋JavaScript。旧 Brookesia 工程、微雪示例与历史资料保存在 `legacy/`，当前固件不依赖该目录。

当前目标板是微雪 ESP32-P4 WIFI6 Touch LCD 3.5，16 MB Flash、32 MB PSRAM、320×480 屏幕，要求芯片 revision ≥3.1。

## 本版状态

源码版本 **0.8.0**，公共核心 C API **8**，脚本运行时 API **1**。本版加入应用子页面导航、分类接口测试 BPK，以及四个快捷项和一条亮度滑条的紧凑原生面板；主题颜色由一份 JSON 生成。包资源、异步私有持久化、19 种控件、通知中心、受控后台和共享软键盘保留。实现及本轮验证状态见 [0.8.0 记录](docs/0.8.0-navigation-ui.md)。

**0.8.0 已构建、烧录并完成本轮五套真机验收。** 快捷面板 6 组、子页面 10 组、框架 14 组、资源存储 12 组、最终分类 API 17 组均通过，未见系统 panic 或意外重启。接口测试首页直接显示应用名，Wi-Fi 列表分批更新；最终 API 与清理阶段无新增脚本错误，早期失败证据保留。临时包/数据与通知已清理，Calculator 包与用户数据、接口测试 BPK 保留，COM10 已释放。固件身份、失败修正和短测边界见 [0.8.0 记录](docs/0.8.0-navigation-ui.md)。历史 [0.7.0 验证](docs/0.7.0-framework.md)、[0.6.0 验证](docs/0.6.0-framework.md) 和 [0.5.0 验证](docs/0.5.0-framework.md) 不替代本版验收。

默认固件只有设置应用。资源监视和接口测试已改为独立 BPK，计数器保留为开发示例。没有任何 JSON＋JS 应用再通过固件启动入口内置注册。

## 当前功能

- 原生桌面：顶部状态栏、应用图标和名称、底部手势条、桌面网格/分页。
- 原生系统交互：顶部左侧跟手下拉通知中心，右侧下拉快捷面板；左右边缘返回、上滑释放回桌面、上滑停住打开最近应用；卡片支持横向切换、恢复、上滑关闭、全部清除及点击空白退出。
- 快捷面板：紧凑时钟/日期/设置头部，Wi-Fi、电源、声音、主题四个独立快捷项；声音进入原生声音设置，主题直接切换明暗。下方仅有太阳图标和宽亮度滑条，不显示数值百分比；语言与字号在设置中调整。
- 应用控件与通知：卡片、列表、列表项、工具栏、分割线、对话框和底部弹层；样式增量合并及主题/状态覆盖；通知发布、更新、取消、持续通知和点击恢复应用。
- 原生设置：首项显示 Ouse 本地账号与头像；保留亮度、主题、语言、字号、电源、音量/静音和 Wi-Fi；底部“关于设备”显示设备名称、版本与硬件规格。设置立即生效、后台保存，失败时提示重试。
- Wi-Fi 设置：开关、当前网络、可用网络列表、信号/加密标记、扫描刷新、连接详情、手动添加与忘记网络；开放网络直接连接，不弹密码输入。
- BPK：应用自己的中英文名称、自定义透明图标和内部词表、包内图片/文本/二进制资源；支持串口安装、更新、列举、打开、卸载和重启恢复。
- 应用数据：异步私有 JSON 存储，关闭与更新保留、卸载清除；资源监视显示实际文件系统、安装包和应用数据统计。
- 应用子页面：一个 `page.json` 定义最多 16 页、8 层导航栈；`Ouse.navigation` 提供前进、返回、替换和页面变更通知。每次只建立当前页面，Home 保留导航栈，显式关闭后回到初始页。
- 共享主题与排版：浅深色语义配色、dp/sp、中英文即时切换、100%/115%/130% 字号；逻辑 DPI 由启动配置提供。共享 14/16/20px 中文 fallback，源字体覆盖约 5,000 常用汉字，详见 [字库说明](docs/0.4.13-fonts.md)。
- 硬件服务：AXP2101 电源/电池快照、背光与自动熄屏、ES8311 音量/静音/测试音/短 PCM 录放、P4 经 SDIO/C6 的 Wi-Fi 与 SNTP。测试录放放在接口测试 BPK，设置只保留日常控制。
- 本地电脑控制：CH343/UART0 的 OPK1 安装协议、控件树/JPEG/触摸注入和 stdio MCP 工具。板上没有网络 MCP 服务。

应用总注册容量为 8 个，其中设置占 1 个，最多再安装 7 个 `user.*` BPK。只有一个前台应用；回桌面默认保留实例并暂停脚本，声明后台能力且主动 begin 的应用可继续受限执行。关闭则释放 JS、控件、回调、定时器及预览。本版没有原生后台音乐、闹钟或倒计时业务 API。

主题的唯一颜色源为 `components/ouse_core/theme/colors.json`。修改后运行 `python tools/gen-theme-colors.py`，同步生成固件调色表、VS Code 预览 JSON 和扩展颜色常量，再构建相应产物。预览先校验并加载全部 12 个颜色 token，再首次绘制；默认应用背景使用 `background`。应用通过语义颜色和 `Ouse.theme` 接收当前明暗模式；这不是运行时从文件加载任意主题的接口。

“关于设备”显示板级规格，不显示资源使用量：ESP32-P4、32 MB PSRAM、16 MB Flash/microSD、3.5 英寸 ST7796 显示、FT6336 触摸、ES8311 音频、8 Ω / 2 W 扬声器、OV5647 500 万像素 MIPI-CSI 摄像头、C6 无线及 AXP2101。规格来自[归档微雪产品说明](legacy/ESP32-P4-WIFI6-Touch-LCD-3.5/README_ZH.md)，是板级静态声明，不是运行时外设探测；列出 Bluetooth LE 不等于本版已经开放蓝牙服务。实时堆、CPU 频率和运行时间由资源监视 BPK 查询，CPU 频率不是 CPU 占用率。

## BPK 开发和安装

当前只有一种 BPK 格式。容器包含四个必备文件 `manifest.json`、`page.json`、`app.js`、`icon.bin`，以及清单 `assets` 声明的最多 16 项资源，使用 ZIP_STORED；清单不包含包格式版本字段。

清单提供默认 `name`、可选 `names.zh_CN/en` 和图标；语言缺项回退 `name`。桌面和最近应用读取包内名称并即时切换语言，不执行应用 JS。PNG 设计图在电脑上转为带尺寸头的 BGRA8888 `icon.bin`，板上直接显示，保留 alpha。应用内部通过 JSON `translations/textKey` 或 JS `Ouse.locale` 管理自己的文案，无需加入系统词表；没有 XML 页面解析器。

详见 [BPK 说明](docs/bpk.md) 和 [应用 API](apps/README.md)。已经生成的独立包：

| 应用 | 文件 | 字节数 |
| --- | --- | ---: |
| 资源监视 | `dist/bpk/resource-monitor.bpk` | 14,001 |
| 分类接口测试 | `dist/bpk/api-test.bpk` | 72,433 |
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

应用产物为 `build/ouse_core_desktop.bin`，对应应用地址 `0x10000`。最终固件为 **7,470,448 字节**，8 MiB 应用分区剩 **918,160 字节**；已通过 COM10 / 921600 应用烧录与哈希校验，保留 NVS 和 LittleFS。五套检查与最终清理已完成，身份和证据见 [0.8.0 记录](docs/0.8.0-navigation-ui.md)。首次空白设备还需要 bootloader 与分区表，不额外生成合并固件包。

保留 CPU 400 MHz、PSRAM 200 MHz、XiP、双核和真实 LCD/Wi-Fi。LVGL 单绘制单元，UI 任务为 48 KiB 内部栈；所有 JS 由另一个 64 KiB 内部栈的任务执行，每实例有 512 KiB JS 堆、40 KiB JS 栈上限，前台/后台每轮预算分别为 12/4 ms。显示 flush 等待继续使用已定位的让出 CPU 方案，等待真实完成而不提前复用 DMA 缓冲；该实现依赖固定 LVGL display 布局，升级时必须重新审视。

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
components/ouse_installer/  OPK1 和 BPK 管理
apps/                       独立资源监视/接口测试 BPK 源码
examples/                   计数器/计算器等 BPK 示例
dist/bpk/                   已打包应用
tools/                      打包、开发、串口控制与主机检查工具
docs/                       当前说明和历史故障/验证记录
legacy/                     旧 Brookesia 工程、微雪示例与历史文档
```

`tools/embed-script-fixtures.py` 只服务主机测试，不参与固件应用注册。桌面移植来源见 `components/ouse_core/BROOKESIA-NOTICE.md`，字体许可见 `components/ouse_core/FONT-LICENSE.txt`。
