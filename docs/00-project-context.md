# 项目上下文

更新日期：2026-10-15。此文件是长期交接入口，不依赖聊天历史。

## 用户目标与背景

- 已购买 ESP32-P4-WIFI6-Touch-LCD-3.5；当前仓库提供微雪示例。
- 希望采用 ESP-Brookesia 架构，做可持续扩展的手持系统，并在云端部署软件商店和服务。
- 系统需要桌面，以及音量、亮度、配网、蓝牙、语言、应用列表、关于等设置。
- 基础应用包括音频播放器、视频播放器、相机、相册、文件管理；需要 SD 卡支持。
- 利用背部 40pin 接口做串口调试器、舵机调试器等工具，并支持包管理与更新。
- 利用高速 USB 做电脑声卡、麦克风、网卡及 USB 转 TTL 桥接。
- 手机联动：显示 QQ/微信通知、微信/支付宝二维码；不希望手机长期打开热点，考虑蓝牙桥接。
- 接入云端 AI。
- 用户了解嵌入式协议、ESP-IDF 和 Arduino，PCB 能力一般，软件能力较好；后续不亲自写代码，但要理解架构、掌控系统。
- 本轮要求：整理需求、保存上下文、可行性评估、架构理解文档。没有要求本轮写固件或部署云端。
- 已明确：手机联动第一版 **Android 优先**。

## 用户固定背景与选型偏好

用户已在聊天中直接提供背景卡，后续无需重复询问身份、技术栈和经历。默认简体中文交流。此处保存与本项目有关的信息，不依赖个人网站是否可访问。

- Ouse，大学在读、软件工程师实习中，专注 .NET；有 AI 对话平台和 Avalonia 上位机开发经验。
- 后端优先 C# / .NET / ASP.NET Core / Web API；桌面与上位机优先 Avalonia；Web 可用 Blazor 或 Svelte / TypeScript。
- 熟悉 STM32、PCB、PlatformIO、nanoFramework，以及 Git、Docker、Native AOT、Godot。
- 现有项目优先复用或扩展：Crystal.Avalonia（模块化、依赖注入、View/ViewModel 注册）、GOZA.Dock（停靠布局）、DotKernel（AI 插件与工具审批）、Blazor.GoldenLayout（Web 停靠布局）。描述来自用户背景卡，尚未核对具体 API 和当前版本。
- 个人站点：<https://0use.net>；GitHub：<https://github.com/0use-TE>。站点使用 SvelteKit / Svelte / TypeScript / Tailwind CSS。
- 架构说明优先用用户熟悉的服务、依赖注入、事件、模块边界类比，但明确 MCU 的任务和内存模型差异。
- 设备固件继续评估 ESP-IDF / C++ / Brookesia；熟悉 .NET 不构成将现有固件迁移至 nanoFramework 的要求。

## 2026-10-14 补充确认

| 项目 | 用户确认 |
| --- | --- |
| 实物配件 | 摄像头、电池、SD 卡都有；外设型号先参考微雪资料，实际工作状态待测 |
| 芯片/协处理器版本 | P4 revision 和 C6 固件暂时未知，后续实机检测，不需要现在追问 |
| 手机 | 三星 Galaxy A56，Android 16，日常可用 Google 服务 |
| 二维码 | 先静态展示，后续希望支持动态付款码；动态方案保留为研究需求 |
| USB 电脑兼容 | Windows 优先，其他系统暂不纳入首版验收 |
| 软件商店 | 自用可信应用，不需要人工审核流程或开放第三方发布体系 |
| 云端环境 | Linux；后端按用户偏好优先 ASP.NET Core |
| 排针资料 | 用户附件是背部排针标识照片；仓库另有完整 schematic PDF，接线前以原理图/BSP/实物核对 |

原理图路径：`ESP32-P4-WIFI6-Touch-LCD-3.5/schematic/ESP32-P4-WIFI6-Touch-LCD-3.5-schematic.pdf`。照片可见供电、GND、I2C、USB、GPIO 和 C6 标识；这不能证明每个脚都可由 P4 应用自由使用。本轮仅查看照片与确认 PDF 存在，尚未做完整逐脚审计。

## 本轮已确认的事实

本地证据根目录：`ESP32-P4-WIFI6-Touch-LCD-3.5/`。

| 事实 | 证据 |
| --- | --- |
| 32MB PSRAM、16MB Flash、P4 主处理器与 C6 无线协处理器 | 产品 `README_ZH.md` 与微雪官网 |
| 板载屏是 320×480 SPI ST7796，触摸是 I2C FT6336 | 产品说明；不能误用其他 P4 板的 MIPI-DSI 屏配置 |
| Wi-Fi/BLE 由 C6 提供，P4 本身没有无线电 | 产品说明 |
| 示例 11 内附 Brookesia Core `0.6.0-beta2`、LVGL `9.4.*` | `examples/esp-idf/11_esp_brookesia_phone/components/brookesia_core/idf_component.yml` |
| 示例 11 初始化屏幕、锁、Phone、样式和本地应用注册；未初始化完整联网/音频/SD 系统 | 示例 11 `main/main.cpp`、`README_ZH.md`、`sdkconfig.defaults` |
| 示例 11 默认关闭 services、AI、speaker | 示例 11 `sdkconfig.defaults` |
| BSP 精确固定 `waveshare/esp32_p4_wifi6_touch_lcd_3_5 ==2.0.2` | 示例 manifest 与 `docs/components_ZH.md` |
| 示例 10 是 MP4/AVI 容器中的 JPEG/MJPEG 路径，不是通用 H.264 视频播放器 | 示例 10 `README_ZH.md` |
| 示例 12 使用 LVGL 8.3，不能与示例 11 的 LVGL 9 UI 直接合并 | `docs/components_ZH.md` |
| 示例支持不同 P4 芯片 revision profile，必须识别实物 | 产品 `README_ZH.md`、`docs/revisions_ZH.md` |

上游资料核对：当前 Brookesia 文档/README 列出 v0.8 master 的 GUI、Runtime、System Core/Super、App Store；v0.7 属稳定维护分支。这里只确认公开实现和文档存在，没有验证在本板运行。参见 [可行性评估](02-feasibility.md) 的来源与版本路线。

## 建议，尚未成为已验证决策

1. 保留微雪示例作为参考，产品独立建工程，避免直接拼接多个 demo。
2. 先通过板级实验和 Brookesia 版本实验决定正式基线；长期软件商店目标优先评估新 System/Runtime 路线。
3. 第一版使用可信内置原生应用、统一服务、整机 OTA；动态应用商店随后接入。
4. 重媒体处理和 GPIO/USB 服务驻留固件；动态包主要承载 UI 和业务逻辑。
5. 手机通知走 Android 伴侣应用 + BLE；云服务先走 Wi-Fi，蓝牙手机代理另行验证。

## 待验证或待用户以后补充

- 实物 P4 芯片 revision、PCB 版本、C6 固件版本；后续检测。摄像头/SD/电池已确认具备，工作状态待测。
- 最新 Brookesia 对本板是否已有 HAL board 配置，托管 BSP 能否通过适配接入；不能假定 P4 支持就等于该板支持。
- 背部 40pin 的逐脚映射、已占用引脚、供电、电平和可用 UART/PWM 引脚；本轮未完成原理图逐脚审计，尚无可直接接线的引脚表。
- 三星 A56 / Android 16 上的通知授权、后台保活、锁屏重连与通知内容可见性；有 Google 服务不等于这些行为已验证。
- 静态二维码图片导入方式；后续动态付款码的官方授权接口、刷新与失效机制可行性。
- Windows 上 CDC/UAC/网络类兼容性，Windows 具体版本后续验收时记录。
- 自用应用包的版本兼容、可信来源、原子更新和恢复机制；不增加人工审核需求。
- Linux 云端的具体部署资源、域名、账户体系和 AI 提供方尚未确定。

此前 [个人资料链接](https://0use.net/api/me.md) 未成功读取；现在用户已直接提供背景卡，上面的固定背景以用户提供内容为准，无需继续因链接不可访问而追问。

## 继续工作的方法

### 最新学习目标

用户明确准备采用最新 Brookesia，并希望一步一步理解桌面、Settings、HAL/LVGL 桥接、隔离、共享服务和应用热更新。本轮新增教程 `04-brookesia-minimal-tutorial.md`，配套 System Super 固件和 JS 计数器。参考源固定为上游 master 提交 `e937455b0db1a3e873b1da61d6d13652f3dcc7e2`，并非旧 Phone 0.6。这是学习工程；微雪 BSP 保持公开 Registry 2.0.2，Brookesia 源码通过本地 reference 用于最新 API 试验，正式产品发布策略另定。

用户最新收敛：**第一次只烧录默认桌面，计数器不能预装进固件。** 固件只含 System Super 和框架 Settings 入口，计数器保持独立 BPK，留待后续安装/更新。不要为了方便首次演示把计数器加入资源 staging 或改成原生内置应用。

本轮已完成：桌面固件在 ESP-IDF 6.1 默认 rev3.1 配置编译/链接/大小校验通过，固件大小 `0x660420`，出厂 LittleFS 仅含 Settings 资源与系统资源，不含计数器。Counter 1.0.0/1.1.0 包均已打包校验，1.0.0 模拟点击加一通过；新版交互与板端热更新未验证。没有烧录实物，也没有验证 rev1 构建，下一步由实物 revision 决定 profile 后烧录桌面。

后续用户已自行烧录并提供日志：实物 P4 为 **v3.2**，PSRAM 测试和 LittleFS 挂载通过，但显示 HAL 未发现。检查链接 map 发现 `waveshare_display.cpp` 未纳入固件。已将注册改为固定符号 `ouse_display_plugin_symbol` 并在 main CMake 添加 `-u` 保留；同时在 defaults 与当前 sdkconfig 关闭 Wi-Fi 服务自动注册。用户要求自行重新编译，助手已停止验证构建；修复后的编译与实机显示尚未验证。C6 固件仍未知。

随后用户确认亮屏，日志进入 System Super 初始化，但在 libc `lock_init_generic()` 创建互斥锁分配失败处 abort。原配置普通 malloc 小于等于16KB优先内部 RAM，setup 任务栈48KB也在内部 RAM。已按官方板配置改 `SPIRAM_MALLOC_ALWAYSINTERNAL=0`，内部保留增加到128KB，setup改PSRAM栈并配对 `vTaskDeleteWithCaps`；初始化函数正常返回后再删任务，确保 C++ 临时对象析构。新增阶段性 internal free/largest/PSRAM free 和无分配失败回调日志。源码确定直接触发条件是锁分配失败，内存策略是待实机验证的缓解方案；尚未重新编译/烧录，仍由用户执行。NVS缺省配置警告不是该abort的直接原因。

后续助手先读本文件、需求与架构手册，再检查仓库当前状态。区分文档计划、编译通过、实物验证三个状态。更新决策时注明原因、版本和验证证据；不要把本轮建议直接写成已完成能力。

下一项最有价值的工作：记录实物信息，跑显示/触摸、SD、音频、Wi-Fi 基线，然后用新 Brookesia 做“桌面 + 设置亮度 + 一个应用包”的最小适配实验。验收见路线文档。

### 最新实机日志：桌面与 Settings 已启动

2026-10-14 用户提供完整日志（附件 `3dd54b74-fe50-4a21-a9f6-9a8ced931b0c`，ELF SHA256 前缀 `9b5e486ab`）。本次约8秒打印 `READY: System Super + Settings + BPK installer`，记录持续至约81秒，包含两次打开/退出 Settings、多页面切换、主题 `dark` 与语言 `zh_CN` 偏好保存成功。记录中的本次启动没有 Load access fault、abort 或分配失败。启动后的内存日志为内部 RAM 剩余444487字节、最大连续块303104字节，PSRAM剩余24830264字节；不代表已经做过长期稳定性验证。

文件开头只有上一轮的 `ELF file SHA256`、`Rebooting` 和 SW_CPU_RESET，缺少其故障正文，不能判断上一轮为何重启，也不能从 Saved PC 在 LVGL flushing 推断 LVGL 导致崩溃。此前另一份日志中的 TLSF 非法访问仍是历史未定位故障，本轮未复现。

剩余能力缺口：Wi-Fi/Network HAL关闭，但 SNTP 仍自动注册，桌面/Settings尝试绑定时报缺少网络连接接口；AudioPlayback未注册，音量禁用；System Device关闭，Settings拿不到 GeneralIface重启接口和BoardInfo接口。当前版本主题/语言先保存偏好，再要求重启；由于重启接口缺失，点击立即重启会进入失败提示，不能将保存成功写成已完成当前界面切换。后续可补系统HAL与停止离线SNTP注册，暂未改固件配置、编译或烧录。

### 本次实现，等待用户编译与实测

用户授权启用System Device、声音、Wi-Fi/BLE，并增加DPI设置。已启用System/Restart、Wi-Fi/Network/SNTP、C6 Hosted/NimBLE；BLE服务在Settings按钮首次使用时显式加入，避免启动时探测未知C6固件。新增微雪ES8311 CodecPlayer与测试音Playback HAL，复用AudioPlayback音量和静音，不使用其他板的Board Manager音频配置。暂不支持媒体文件解码、录音、经典蓝牙音频或手机通知协议。

Settings复制到仓库管理的`components/brookesia_app_settings`并调整manifest/CMake，新增测试音、BLE广播控制和75%/100%/125%DPI比例。DPI保存于NVS `ouse.device / DensityPct`，main启动读回density，修改后重启生效。defaults与已有sdkconfig已同步，实际编译后生效值仍待验证。C6 SDIO引脚已对照完整原理图：18/19/14–17，Reset54，Slot1；C6固件未知，不能保证启用即成功。

本次没有编译或烧录，保持用户自行执行；不能把代码完成写进COMPLETED.md。重新构建后需更新LittleFS设置页面。步骤与验收见Settings指南第9节。

2026-10-14更新：按用户要求移除当前固件蓝牙功能，保留Wi-Fi、声音和DPI设置；前文蓝牙描述属于历史实现或后续规划。

### 全屏下拉快捷面板，等待实机验证

用户要求平时保留状态栏，顶部下拉后像手机一样占满屏幕，底部上滑收起。已将官方System Super 0.8.3复制到本地`components/brookesia_system_super`维护扩展，主manifest改用该目录；固定参考源码不变。新增面板直接调用Display/AudioPlayback服务调亮度、音量和静音，Wi-Fi暂仅展示连接状态。展开后取消旧状态栏1.5秒自动隐藏计时，全屏拦截触摸，底部24dp上滑区域或关闭按钮收起；面板内控件滚动不会退出应用。收起回到原桌面/应用，不停止当前应用。

ESP-IDF 6.1构建通过，固件`0x747710`，8MB应用分区剩余约9%；JSON、依赖、动作连接静态检查通过。新页面随LittleFS打包，更新需要主固件和LittleFS。尚未烧录验证，因此不写入COMPLETED.md。说明见`07-desktop-quick-controls.md`。没有实现通知中心、最近任务、蓝牙或左右返回。

随后用户实机反馈亮度和音量可以设置，但当前位置滑钮不可见，静音/关闭按钮缺少边框。本轮仅修改面板JSON：增加indicator和knob的partStyles，按钮显式2dp边框、内边距和按下样式。该视觉修正只需要更新LittleFS；不据此推断静音、收起手势和所有主题/缩放均已通过实机验收。

### 启动画面恢复

用户反馈开机启动画面消失。源码确认原因是此前闪屏修复在`System::start()`返回后才打开背光，而Core已在该方法内移除启动覆盖层。main新增产品子类`PocketSystem`，在`on_init()`最开始强制刷新已挂载的启动画面、等待100ms后打开背光，再调用Super的`on_init()`继续初始化；设置覆盖层最短挂载时间600ms。保持重启前关灯、启动时黑屏清除LCD旧GRAM；初始化/启动失败会关灯，关闭启动覆盖层的构建仍在桌面就绪后点亮。这次修改主固件，视觉时序待重新烧录验证。

修复后ESP-IDF 6.1构建通过，固件大小`0x748130`，应用分区仍剩余约9%。烧录导出目录同步最新主固件；本轮无页面资源变化，只需更新0x10000的主固件。启动日志新增`Startup overlay visible; preparing desktop`，用于与`READY`核对先后顺序。

### 下一步系统能力核对：最近任务和电量

用户希望回桌面时保留应用，在最近任务列表中单独关闭。当前Super底部手势调用`close_active_app()`→`stop_app()`，确实直接停止当前应用。Core已有`pause_app/resume_app/stop_app`，但pause仅调用应用生命周期回调并改变状态，不自动暂停所有FreeRTOS任务，也不替产品桌面完成隐藏/切换界面。微雪旧Phone的Manager已实现短上滑Home、上滑停留RECENTS_SCREEN、删除任务卡片发送STOP；可参考交互，不能直接复制旧版API到新版Super。

微雪仓库中的Phone状态栏初始化调用`setBatteryPercent(false, 100)`，main只定时更新时钟，没有真实电量刷新调用；320×480样式开启电池图标，关闭百分比文字。因此仓库示例图标是默认满电展示，不能据此确认用户到手固件采集了真实电量。

原理图PMIC区明确为AXP2101，经ESP I2C SDA/SCL连接；BSP对应SDA GPIO7、SCL GPIO8。最新HAL已有AXP2101 BatteryIface适配，但需要Board Manager的匹配设备和power_manager接口，不能仅打开POWER_DEVICE便认定本板已接入。当前固件POWER_DEVICE关闭，Super状态栏未接电量。后续建议接入电池电压/百分比/充电状态，再显示于状态栏和快捷面板，同时补自动息屏与唤醒。此轮仅核对和记录，没有修改固件或实现最近任务/电池功能。

### SD卡存储设计讨论

用户已有SD卡，询问应用/日志放置和拔卡处理。建议系统固件、桌面/Settings资源、关键设置和小型动态应用默认内部Flash；SD作为可移除扩展存储，用于媒体、用户文件、下载包和导出日志，后续允许显式选择SD安装动态应用。无卡必须仍能启动桌面和Settings。日志采用有容量限制的RAM环形缓冲，SD批量落盘，少量关键故障摘要保留内部Flash，避免无卡时无限写NVS。

源码核对：Core支持Internal/External/Auto安装目标；Auto在有外部卷时优先选择外部卷，因此接入SD后应明确选择Internal作为默认。外部卷识别/安装支持不代表热插拔已完成。微雪BSP提供FAT/SDMMC挂载与卸载，但slot配置为`SDMMC_SLOT_NO_CD`，不能依赖已有卡检测中断。产品还需统一存储状态及I/O错误处理、安全移除（停止新I/O、刷新并关闭文件、卸载）和意外拔卡降级。拔卡后仅依赖SD的应用/媒体功能不可用，系统保持运行；写入中硬拔可能损坏FAT或当前文件，无法承诺无损。挂载失败不能自动格式化。以上为设计建议，尚未实现或确认为最终存储策略。

### 2026-10-14 三项基础能力实现（待实机验收）

用户“那完成！”授权实现。新增 `pocket_platform` 常驻服务和只读 AXP2101 BatteryIface，接入 SDMMC/FAT（不格式化）、后台文件复制、电量缓存、NVS 息屏偏好和触摸唤醒。Settings 独立电源页，状态栏百分比；未实现深度睡眠/关机/电源键。SD 没有 CD 检测，约5秒检查状态，重新插卡需点连接；复制期间拒绝安全移除。内部文件入口限定 `/littlefs/user`，不展示系统/应用运行目录；SD 不作为 Core 应用安装卷，默认安装位置强制内部。

预装 `examples/file-manager/src` 的 JSON/JS 包 `net.0use.files`，可用 `tools/package-file-manager.py` 构建开发 BPK。文件应用支持分页、目录返回、新建、重命名、普通文件复制、文件/空目录删除（二次确认）；不支持媒体预览、目录递归复制。计数器仍未预装。

为最近任务新增本地 `components/brookesia_system_core`（官方 Core0.8.4 快照拷贝，依赖指向参考库），不修改 `.reference`。应用 Stack 挂载，pause 隐藏当前screen-flow根，resume恢复并刷新前台，已暂停应用start转resume。Super短上滑Home、停留450ms最近任务、名称卡片恢复/关闭/上滑关闭，最多3个非桌面应用按使用顺序回收。统一 `system.navigation.back` 只派发前台，键盘/对话框优先；C++根页返回unexpected("navigation.home")，JS根页返回字符串同名，由Core转交Super。暂停不强制冻结应用自建任务。

ESP-IDF6.1完整构建通过：主固件 `0x75ad60`，8MB应用分区余约8%。静态资源/依赖检查、文件应用行为测试通过；实际LittleFS镜像确认Settings、Files/profile、电源页、最近任务资源存在，Counter不存在。四文件复制到 `examples/brookesia-minimal/build/bootloader/bin` 并核对SHA256。本轮必须同时更新主固件和LittleFS；重烧LittleFS会替换其文件和已安装应用。所有硬件效果仍待实机验收，未更改COMPLETED.md，未Git提交或烧录。完整使用、接口、限制、验收顺序见 `docs/08-files-power-navigation.md`。完整日志系统和媒体播放器不在本轮实现范围。

### 2026-10-14 新页面字体单位导致启动失败：已修复，待重烧验收

用户实机日志报告 Settings 首页 `Field 'font_size' must use sp units`，随后背光从80%设为0%。原因是新增资源生成器把标签字体写成 `16dp`（电量为 `12dp`），违反当前 GUI parser 的字体 `sp` 要求；注册应用解析失败使 System 初始化返回错误，main 的失败处理主动关闭背光。这不是该日志中的内存崩溃或屏幕驱动未启动。

已修正生成器并重新生成 Settings 首页/电源页、Files 和 Super 电量/最近任务资源；静态检查补充所有产品资源的字体字面量单位校验（61项通过），文件应用原有行为测试通过。ESP-IDF6.1构建通过，实际 LittleFS 镜像逐一检查上述四个页面字体及状态栏字体常量，确认 Files/profile 存在、Counter仍未预装。导出目录四文件SHA256核对通过。

主固件SHA256仍为 `9B133934B199A48E5D8791B9847860CC987B6412977AC6A652FC04F0A39723C9`，与修复前相同。已烧上一轮三项能力固件的设备，本次只需更新导出目录 `littlefs_data.bin` 至 `0x810000`；LittleFS重新烧录会替换其中的应用和文件。未实际烧录，启动恢复和新功能仍待实机验收；不更新COMPLETED.md。

### 2026-10-14 最近任务重复动作导致桌面启动失败：已修复，待重烧验收

用户再次提供日志：Shell 启动报 `Document action must be unique: 'ouse.recents.close0'`，卡片本体与其关闭按钮同时占用该动作名，System start失败后背光关闭。此前仅修正字体单位，未验证GUI文档动作唯一性，遗漏了这个独立错误。

生成器及实际资源将三张卡片的上滑动作分别改为 `ouse.recents.swipe_close0/1/2`，关闭按钮保留 `ouse.recents.close0/1/2`；Shell新增相应订阅并复用停止任务逻辑，仍仅上滑方向触发手势关闭。静态检查新增按文档检查已加载screens/templates的字面量动作唯一性（不扫描未加载模板），先复现原始重复动作失败，再验证修复通过；检查三个手势/按钮动作及C++订阅连接。此检查不替代运行时模板展开与实机启动验证。

ESP-IDF6.1构建通过，主固件 `0x75ae90`，应用分区剩8%。实际LittleFS镜像核对Settings/Shell/Files全部63个已引用资源与源文件一致，100个字面量动作无重复，三张卡片手势/按钮动作分离，Files/profile存在，Counter未预装。文件管理器既有行为测试通过。四文件重新导出至 `examples/brookesia-minimal/build/bootloader/bin`，SHA256核对通过；主固件SHA256为 `E93E794CCE51E40ECC0E6DBEB75EBA420CBB1C886367FE0864520D834C7EF64A`。

本次修改了动作订阅代码和页面资源，必须一起更新 `0x10000` 主固件与 `0x810000` LittleFS；分区表/bootloader未变化。LittleFS重烧会替换其中的应用和文件。未实际烧录或确认桌面恢复，不更新COMPLETED.md。

### 2026-10-14 Files 服务声明与应用切换修复（待实机验收）

用户本次21:57日志确认桌面已经启动，Files失败原因明确为 `Pocket (service is not declared in app manifest)`。已在Files清单顶层声明Pocket 1.0.0服务，并重新生成BPK与LittleFS。此前测试未按真实清单限制服务调用，现已补上该检查。

源码确认另一个独立缺陷：GUI Stack重排再次调用LVGL后端mount_screen时，无条件清除HIDDEN，可能暴露已暂停的Settings。新增本地 `components/brookesia_gui_lvgl`（官方0.8.5快照），保留record->hidden，不修改.reference。Super统一切换应用时暂停旧前台、激活目标，失败恢复旧前台；Core补pause/resume状态保护，Running重激活不重复调用on_resume。应用前台时隐藏Launcher主体，保留系统覆盖层。

侧滑改用实际起止坐标判定，避开顶部/底部，启动动画期间忽略导航；底部快速上滑回Home，上滑停留450ms打开最近任务。应用中顶部轻触显示系统状态栏，1.5秒自动隐藏，下拉仍打开全屏快捷面板。Settings自己的页面标题栏属于应用内容。最近任务改为横向滚动名称/状态卡片，可恢复和关闭，尚未实现微雪旧Phone的屏幕截图缩略图。无图标应用启动动画不再引用不存在的default图像；移除已删除BLE控件的文案更新，修正新增Power标签的绑定键。

静态资源检查、真实Files清单/UI约束下的JS行为测试通过；MSVC本机测试抽取真实LVGL挂载方法，参考版复现暂停画面暴露，本地版通过隐藏/恢复/重新挂载，真实C++导航策略边界检查通过。这些测试不覆盖触摸硬件和像素渲染。ESP-IDF6.1构建通过，主固件 `0x75c570`，8MB应用分区剩8%。实际LittleFS中63个已引用资源与源码一致，100个字面量动作唯一，Files清单/JS/profile/UI及两种语言核对通过，Counter未预装。

四文件已导出并核对SHA256至 `examples/brookesia-minimal/build/bootloader/bin`。主固件SHA256为 `AD6200470D4804162AFDB9EAD1EAC0D47264CCF59F907A145768203F5E80BC7F`，LittleFS为 `6B8AA129D124738B709201E9B029C9961A5C7969901D0D0D22A8B10E3FFC2B43`。本次一起更新0x10000主固件与0x810000 LittleFS，bootloader和分区表未变化；LittleFS重烧会替换其中的应用和用户文件，先备份。尚未实机验证，不更新COMPLETED.md，不Git提交或烧录。C6固件版本不匹配警告仍待另行处理。

### 2026-10-14 按微雪旧 Phone 实现截图最近任务（已获用户实机确认）

用户要求完全参考随仓库微雪旧 Brookesia Phone 的逻辑。已核对 Base Manager/App、Phone Manager 与 recents_screen：暂停后截图，保留页面/状态，横向截图卡片居中吸附，点击恢复、上推关闭、全部关闭。旧版源码是横向卡片，非三星重叠堆栈。本轮将这些行为迁移到本地 Core/Super/LVGL 后端，旧示例和 `.reference` 保持不改。

Core 隐藏应用前调用截图钩子；LVGL 后端合成当前内容和 Top 标题栏，排除桌面及系统弹框，截图后恢复原对象父级/位置/层级。同尺寸 RGB565 缓存复用，关闭时释放；320×480 每张约300KiB，最多三张约900KiB，不含页面及绘制开销。卡片沿用微雪180×302配置（预览180×270、间隔40、圆角20），左右切换、上推关闭、短滑复位、点击恢复及全部关闭；兼容明暗主题。事件经过GUI/Shell输入线程，代次过滤过期事件，不在LVGL回调中同步停止应用。实际指针测试曾检出程序滚动未临时开启SCROLLABLE，现按微雪逻辑修复并通过。

侧边新增透明触摸拦截及跟手返回箭头，达到阈值变蓝，松手隐藏；仍只向前台应用派发统一返回动作。暂停保留 JS 环境及变量，不强制冻结自建 FreeRTOS 任务；框架应用定时器在暂停态不执行，应用自己的 on_pause 可控制业务。STOP 成功后卸载 JS runtime，重新 START 创建新环境，处理本次日志中重复打开后 `SyntaxError: redeclaration of 'gui'` 的运行时复用原因。

按用户要求取消 Files 预装，开发源码保留在 `examples/file-manager`；主工程配置时清理旧生成stage，不删除SD内容。最新 LittleFS `/apps` 仅含 Settings，Shell资源在 `/system/super`；计数器仍不预装。电源服务及设置保留。

ESP-IDF6.1最终构建通过，主固件 `0x760fc0`（7,737,280字节），8MB应用分区余 `0x9f040`（约8%）。资源/动作/依赖静态检查、真实后端隐藏/恢复与导航边界测试通过；实际LVGL软件渲染测试通过截图像素/标题合成、原场景恢复、静态预览/复用、卡片资源生命周期、明暗主题、返回提示和真实指针下点击/横滑/上推/全部关闭事件。实际LittleFS镜像121个文件与staging逐字节一致，Files和Counter不存在，LV_USE_SNAPSHOT已开启。测试不覆盖设备触摸驱动、帧率和真实后台业务，实机尚未验收。

四文件重新导出至 `examples/brookesia-minimal/build/bootloader/bin` 并校验SHA256；主固件 `019182FD5D72570EACB665009764F57C2B1BB9E1855CA237B56A95C19AD3040E`，LittleFS `3DDFA0C052BC61F2461C3108EF408FCCDA8E622653C75E0CC2D7ADFFE543F116`。本轮必须一起更新0x10000主固件及0x810000 LittleFS；重烧LittleFS替换原分区内容，先备份。不更新COMPLETED.md，不Git提交或烧录。使用说明见 `docs/08-files-power-navigation.md`。

随后用户反馈「现在非常完美」，确认本版效果，并要求更新完成清单及本地Git提交。根目录清单新增截图最近任务、保留/恢复、左右切换、上推关闭、全部关闭、侧滑箭头与明暗主题；不将文档、未预装Files或未验证计数器记为完成。本次提交保存当前产品组件、配置、资源生成工具、测试及文档，不包含构建产物或微雪嵌套参考仓库；不推送远端。

### 2026-10-14 Settings 偶发纯色画面：启动可见性补丁（待实机复测）

用户随后报告 Settings 有时正常打开、有时纯色白屏，进入最近任务再点击 Settings 可恢复。日志显示正常 START（324ms），未报崩溃。代码检查确认 START 与 RESUME 不对称：RESUME 调用 set_app_visible(true)，START 在挂载及on_start后未显式恢复页面；Settings启用preload_dom，PAUSE设置的内容/标题根hidden会随STOP后的缓存保留，Backend为避免Stack重排暴露暂停应用又刻意保留hidden。因此存在「启动成功但页面仍隐藏」的明确缺陷，与恢复后显示的现象相符；尚不能凭旧日志断言所有偶发白屏均由此引起。

已在Core成功on_start后、标记Running之前恢复当前screen-flow根可见性，失败走现有启动回滚，不改变后端保留暂停隐藏的策略。加入 `App screen visibility: id(...), screen(...), visible(...)` 日志。MSVC检查原有Stack隐藏/恢复、手势边界及最近任务渲染/输入通过；额外执行真实Core页面显示方法，确认缓存内容页/标题栏从hidden恢复、像素能渲染。测试使用后端记录与调度替身，不覆盖设备时序。

ESP-IDF6.1构建通过，主固件 `0x7611a0`，SHA256 `A32026EDEA58C3F2FDC32F48ADA6B299858FE877BA5FEB93945ED0EFA743AEB2`；四文件重新导出校验。LittleFS SHA256仍为 `3DDFA0C052BC61F2461C3108EF408FCCDA8E622653C75E0CC2D7ADFFE543F116`，bootloader/分区表也与上一版一致。设备已经是上一版截图最近任务时，本次只需更新 `0x10000` 主固件，无需重写LittleFS。未提前记入完成清单，未新增Git提交或烧录。

### 2026-10-15 最近任务自由拖动与默认电池菜单（待实机验收）

用户反馈启动白屏「应该是解决了」，随后要求去掉最近任务卡片上下移动的限制，并将电池设置入口做成默认样式的左侧图标菜单。旧逻辑将垂直位移clamp在±关闭阈值内，已去除。关闭阈值仅保留为松手判定，整张卡片可跟手到屏幕边缘。实际LVGL9.5检查发现仅添加OVERFLOW_VISIBLE仍会被原slot/table绘制范围裁剪；最终采用垂直拖动时临时重挂到全屏host，保留其他卡片的横向位置。上推松手关闭时不先跳回原位，下拉或短滑复位；面板隐藏/刷新清理已移出的卡片。未加入惯性抛掷或退出动画。

电池入口并入Settings主列表，复用官方nav_menu_item的图标/标题/箭头/行高/按压与主题样式。绿色42×42电池图标衍生自微雪旧Phone Apache-2.0组件的白色电池PNG，资源索引新增home.power。英语/中文更新路径迁移到 `/settings_home/page/main_list/power/title_box/title`，使用模板声明的labelProps.text键；生成器可重复运行。

真实LVGL指针检查通过250px向下拖动、200px向上拖动、屏幕顶部超出原slot的预览像素、短滑复位、横向切换及正确关闭事件；缓存Core内容页/标题栏恢复与已有截图/主题检查仍通过。静态资源检查通过，实际LittleFS122个文件与staging逐字节一致，电池模板及PNG存在，Files/Counter仍未预装。ESP-IDF6.1构建通过，主固件 `0x761220`，四文件已导出并核对SHA256。主固件 `1B06496B17642AAC8142A3B22A6106DFB58923E6F5898C4C989F95A5B1268736`，LittleFS `013E663F8035ACE62BDBA2158B1740811EEE42177787D3BFDB58B0093682331A`。本次UI资源有变化，必须同时更新0x10000主程序及0x810000 LittleFS；覆盖LittleFS前备份数据。尚未实机验收，未更新完成清单、未新增Git提交或烧录。
# 2026-10-15 本轮待验收

2026-10-15 主题重复重启日志（附件 `ede91145-5dab-418c-9f34-fc4c15f0c5cb`）：首次为用户确认后的 System HAL 正常重启；随后启动时发生 `Load access fault`，panic 导致第二次重启，最后一次启动成功。使用 SHA256 完全匹配的 ELF `f1bcf626252970d5ec8c6cb769ce37d39794c2fd10f964009cc1e233c5bb4782` 解码：`MEPC 0x48567326 → xQueueSemaphoreTake`，调用链包含 `Usb::Impl::update_connection_state → pthread_mutex_lock_internal`。故障队列地址 `0x48746cb0` 在 PSRAM，普通动态 FreeRTOS 队列应在内部 RAM；无法单凭日志判断句柄失效源头。

本轮将 USB 0.8.1 纳入本地 components，Core及main显式引用，状态/发送锁改为提前创建的内部 RAM FreeRTOS mutex，获取前校验地址。开启轻量堆越界检测，启动阶段检查完整性，打印 `esp_reset_reason`。这是针对故障路径的加固和诊断，尚不能宣称实机重启稳定性已解决。主题快捷入口不再显示 Light/Dark，保留“主题 / Theme”及待重启提示/确认。此前 UI 修复包括电池主题变体、快捷图标contain、滑钮端点余量、Settings标题栏/内容区分离、导航摘要分行及主题卡片自适应高度。GPIO/USB资源管理器不在本轮范围。

最终验证：真实 Brookesia parser/validator、LVGL 布局夹具（中英文、明暗、75/100/125%）以及最近任务/缓存可见性回归通过；ESP-IDF6.1完整构建通过，主固件 `0x7657d0`，分区剩余约8%。核对compile_commands确认USB使用本地组件，LittleFS镜像173个文件与staging逐字节一致，标题/overlay staging与最终源码一致。四文件导出到 `examples/brookesia-minimal/build/bootloader/bin` 并核对SHA256：主程序 `AFCC2842A1673AEE05A53BB1594449790F79F0593B1CAD8529341BB65B1F229C`，LittleFS `0DF8059CE70CB4446C271FC4CBCCB4F6C6C4977A39285C5A4622816A2A4C58D9`。需同时更新0x10000和0x810000；LittleFS重烧覆盖内部文件/已安装应用。没有烧录设备，没有新增Git提交，没有将待实机项目写入完成清单。

桌面改为固定网格左右分页，取消纵向滚动；快捷面板改为主题图标卡片和水平滑条，移除关闭按钮与提示文字。下拉、底部上滑采用跟手拖动及松手吸附，展开手势结束前控件禁用，防止误调亮度/音量。状态栏只显示电池图标，下拉显示百分比及充电状态。待设备验证后再更新根目录功能完成清单。

最近任务提前关闭待复测：展开时消耗原始上滑；关闭需连续松手 80ms，接触恢复取消待关闭并保留拖动锚点，滑出动画结束后才停止应用。真实 LVGL 已验证短暂断触恢复及定时器清理；现有设备日志不足以确认原始触摸释放的原因。

2026-10-15：用户澄清按住时预览消失、松手恢复，上滑仍能正常关闭；已用与生产 JSON 一致的 flex-column 根容器复现布局覆盖拖动坐标。原生主体和拖动卡片添加 IGNORE_LAYOUT，明暗主题下按住和轻微上滑的真实截图像素回归通过。主题快捷入口改为保存同一 GUI 偏好并提示“立即重启 / 稍后”，按钮显示“重启生效 / 待重启”；Settings 恢复前台同步偏好。状态栏换为横向五格电池，按20%分档，充电/未知独立图标，百分比仅下拉显示。ESP-IDF 构建及资源/主题行为回归通过，已导出 build/bootloader/bin；需更新0x10000主程序和0x810000 LittleFS。等待实机复测，不记入完成清单。

### 2026-10-15 用户确认当前版本正常

用户回复“没问题了，记录下”，确认当前桌面、快捷面板、最近任务、电池图标、Settings布局及主题重启效果，根目录 COMPLETED.md 已追加功能列表。主题重复重启加固在本次复测中未再报告异常；未进行长期压力测试，先前锁句柄异常的根因仍未独立证实。

计数器仍未预装、未实机安装或验证热更新，不计入完成清单。现有独立 BPK 为 net.0use.counter 1.0.0（每次加1）和1.1.0（每次加10），包内manifest和入口已核对。安装通过USB服务的UART0控制通道，与console共用CH343串口及115200波特率；默认安装到内部 /littlefs/apps，开机扫描manifest恢复应用列表。计数值只保存在内存，停止/重启后归零。安装步骤补充在 examples/counter/README.md。本轮仅更新记录和说明，未占用串口、烧录设备或新增Git提交。

### 2026-10-15 睡前修复及独立应用（待实机）

用户随后提供 b321ad50-2230-4ee3-843b-0db4ddb65cca 完整日志及两张截图，授权自主修复不透明 Settings 图标、布局、重复开机过场、密码/SSID 乱码，并实现独立音乐和日志应用、检查桌面回归。未要求提交 Git；未烧录、未占用串口、未新增 Git 提交。

实际 LVGL 原配置为 ASCII；密码 UTF-8 圆点在第二次输入时被按字节计数，导致遮罩异常增长。已改 defaults/实际配置为 UTF-8 并加编译断言，真实 LVGL 测试先复现旧错误，再通过连续64个8、长度限制、删除、显示密码、中文与光标输入。设置首页 JPG 换透明 PNG，按主题染色，导航标题/摘要以24dp/18dp网格行及6dp内边距稳定布局；自动行高曾在真实渲染夹具中撑大，已修复并加入文字/行边界断言。

旧 ELF SHA256 a3a9aa084a36aba86cea53bce136f30f3652f4feaaf8111eb1e07097550076ef 匹配用户日志，两次 panic 分别经过 Asio 的 pthread_mutex_lock_internal 和 esp_vfs_select，后者读取有效只读 lwIP select ops 常量也 fault。重复动画伴随 panic 重启。关闭 PSRAM XiP/指令/rodata搬移，普通小块分配256字节内优先内部RAM，保留PSRAM UI数据及外部任务栈、已有堆检测与reset reason。此为规避，根因和设备反复重启稳定性仍未证实；需要新日志与匹配的新ELF继续定位，不能宣称彻底根治。

音乐为 examples/music/src 的独立 JSON+JS 应用，参考微雪旧SquareLine UI（原页没有真实播放器）。固件扩展 ES8311 HAL 接通MP3/16-bit PCM WAV，8–48kHz单/双声道；播放、暂停继续、前后曲、音量及原生顺序播放。SD目录/sdcard/music，内部目录/littlefs/user/music；无快进或ID3封面，进度条按消费字节显示，已播放时间按PCM计算。后台保留播放，关闭最近任务卡停止/释放；SD读取有文件租约，拔卡错误关文件后才卸载，不是GPIO/USB通用资源管理器。

日志后端位于 Pocket，ESP_LOG回调只格式化及写64×384字节环形缓冲，UART输出保留；每两秒写内部/littlefs/user/logs的3个约64KiB轮转文件。轮转失败停止追加并报告persistent=false。独立日志查看器位于examples/log-viewer，具备实时/历史、错误文本筛选、暂停及两次确认清空；后台停止自身轮询。ROM/底层panic转储不经此hook，最后约两秒可能未落盘，不替代core dump。

通过真实parser/validator及LVGL布局/软件渲染的中英文、明暗75/100/125%检查，新增透明像素与控制宽度、文字高度断言；最近任务/缓存可见性/侧滑回归通过。两个实际JS入口与Manifest/GUI路径回归通过，覆盖重载、无SD、缺目录、控制、后台、定时器及过期回调。实际Playback C++用硬件/解码替身验证格式切换、扩容、边界、暂停关闭与文件释放；实际日志后端用真实主机文件验证轮转、失败恢复、UTF-8、游标和新进程读取历史。不能以主机替身证明真实声音及拔卡可靠性。

ESP-IDF6.1最终构建通过，主程序0x77c030，8MiB分区余约6%。实际LittleFS181个文件与staging逐字节一致，Settings/Shell源码与staging一致，出厂/apps只有Settings；Music/Logs/Counter/Files均未预装。四文件导出到examples/brookesia-minimal/build/bootloader/bin并核对SHA256：主程序DDD63B5EB434F3E1F4F44503F30AFE13959B59CAE4297F2749FC4C98C0877D44，LittleFS11C8ADE9B7F798AE5FE35A48F72697EB20029C0616746947C2661901C5F65E22，新ELF A5D71947B356B87F179AF48A717F88A8FBB043016E95A32C382ACF8002B5AC03。建议本次按导出说明更新四文件；重烧LittleFS先备份用户数据。

独立包已生成：examples/music/dist/net.0use.music-1.0.0.bpk（SHA256 bbec46c9e71f755b8a77a38b40537a438b27419e3b4e93e89a21dbdb114e2d41），examples/log-viewer/dist/net.0use.logs-1.0.0.bpk（18da2ba7a1c091a7cc84a299cd3392a66f4de1f334f7cfc182e05d62e2d524d2）。安装命令/用法在各README，综合记录在docs/10-overnight-stability-and-apps.md。未将本轮待实机功能加入COMPLETED.md，保留用户此前确认的完成记录。

### 2026-10-15 后续完整日志纠正 XiP 配置

设备已接到当前电脑，用户明确后续不再复制产物到build/bootloader/bin；保留标准build产物，不自动烧录。

完整附件533fcc87-0458-463b-a96a-b18c6a166122确认芯片v3.2、ELF前缀a5d71947b与当前完全一致，启动在Flash cache_utils.c:114触发esp_task_stack_is_sane_cache_disabled断言，SP=0x4800ed90。上一轮关闭XiP却保留setup及Brookesia workers的PSRAM栈，是本轮已证实的配置冲突，现撤销关闭XiP方案，恢复SPIRAM_XIP_FROM_PSRAM/FETCH_INSTRUCTIONS/RODATA=y。main.cpp增加编译期冲突检查，check-device-integration.py检查defaults，实际sdkconfig同步。此前时钟中断Store access fault是否同源未证实，不宣称恢复XiP根治全部历史故障。原匹配ELF备份在build/diagnostics/a5d71947b。

2026-10-15 后续附件74b889a8-fb54-4dd2-be25-35d1c3a1111d：恢复XiP后系统已进入主题/语言读取阶段，随后Load access fault在lock_acquire_generic读取0x4ff8debc，调用链pthread_cond_timedwait → Boost condition_variable → boost::this_thread::sleep_for → TaskScheduler空闲轮询。当前build ELF SHA256为3B7E436CF4863512AD4B724ABB34907160D3CF19666B4D7A25263F0D0C823252；该附件未包含完整启动SHA，不能以当前ELF哈希直接断言设备匹配。SDK源码与Boost实现显示该sleep_for每轮创建临时mutex/condition_variable，当前无法证明对象失效根因，也没有证据认定Boost或SDK本身有缺陷。

按用户“开启xip，检查为什么报错”，确认defaults和实际sdkconfig三个XiP配置均为y。对已定位等待路径做局部修正：components/pocket_platform/cmake/scheduler_poll_wait.cmake从main/CMakeLists引入，仅把Brookesia task_scheduler.cpp空闲sleep_for替换为本地scheduler_poll_wait.hpp的vTaskDelay。保留轮询间隔，向上取整至少1tick，避免忙轮询，worker返回循环顶部继续检查中断/停止；停止唤醒不再通过Boost条件变量立即发生，空闲时等待至本轮delay结束。使用生成源覆盖，不改.reference，不改全局pthread及其他mutex。覆盖位置缺失/重复时CMake失败，防止上游更新静默失效。

tools/test-scheduler-wait.py已通过真实主机CMake生成（替换源、移除原源、保留flags）、替换失败分支和实际延时helper在100/1000Hz下零值/向上取整/大值边界测试；静态集成检查通过。未构建或烧录ESP固件，由用户操作；设备根因和修复效果待复测。构建应出现“OusePocket: scheduler idle polling uses FreeRTOS delay”。本轮无LittleFS资源变化，可只更新主程序，避免覆盖已安装应用/日志。

### 2026-10-15 对照微雪示例排查启动内存不足（待实机）

附件db51ea78-20b2-45d9-a798-f1f29cee5c76显示Wi-Fi SDIO reset阶段84字节、caps=0x804分配失败，随后libc lock_init_generic创建锁失败abort。SDK本地源码确认caps为INTERNAL|8BIT；可以确认内部可用堆不足，不能仅凭这段截断日志确定全部占用来源。

对照微雪04_wifistation和11_esp_brookesia_phone：当前使用微雪BSP 2.0.2初始化面板/触摸，SDIO引脚和reset保持原板设置；旧Phone defaults关闭Brookesia服务，不能当作同时启用新Core/Super、Wi-Fi及USB的内存验收。当前显示缓冲20行、小于已安装微雪BSP的50行，没有证据更换显示驱动。Brookesia P4官方Wi-Fi及thread_config示例的小对象内部分配阈值为0，本项目此前改成256。

本轮将SPIRAM_MALLOC_ALWAYSINTERNAL恢复为0，保留128KiB内部保留池；启用Hosted 2.12.3自带ESP_HOSTED_DFLT_TASK_FROM_SPIRAM，将Hosted任务栈迁入PSRAM，TCB及SDIO DMA仍由驱动分配内部内存。三个XiP选项继续开启。enable-device-config.py同时维护defaults和已有sdkconfig，静态检查覆盖这六个实际配置，避免只改defaults不生效。分配失败回调增加ROM输出的内部free/largest/minimum及PSRAM free，增加显示和platform阶段内存采样；未改全局pthread或.reference。

静态集成检查通过；用当前ESP-IDF真实Kconfig重新解析并输出独立检查配置，确认六个选项均被保留。尝试ESP32-P4编译器语法检查时，编译器本身无法启动，Windows返回WinError 623（无效的系统DLL重定位），连--version也失败。因此本轮未生成新固件、未烧录、未复制到bootloader/bin，不能宣称实机修复。现有build缓存还包含旧D:/Projects/OusePocket路径，验证时只在diagnostics中生成重定位后的配置输入，不覆盖原构建记录。后续应在可工作的ESP-IDF终端重新配置/构建并实机验证。本轮无LittleFS资源变化，不需要重烧LittleFS。

### 2026-10-15 原生 pthread 与服务线程内部栈修正（历史阶段，后续仍复现）

用户随后清理迁移后的旧build并重新构建烧录，提供附件a11ecc87-11fa-4c5a-8767-69dadc2a529f。ELF SHA256 9805e17cd08b14cdcec3e2eca3231fac2ef8a47bc4bdcaddf608d51169f301e7完全匹配。两次Core1异常分别为VFS select数组读取和USB函数栈保存指令，不能据函数名继续认定USB锁损坏。启动采样内部free385863、largest253940，本次没有分配失败记录。用户要求对照微雪和官方、修复当前桌面异常。

已对照微雪旧Phone/BSP、当前Brookesia和ESP-IDF pthread源码及P4勘误，修正ThreadConfig::apply遗漏inherit_cfg初始化的明确缺陷；产品CMake overlay恢复SDK原生pthread mutex，去掉全局锁包装；服务主调度器和工具线程改内部栈，显式保留Pocket/媒体依赖的第二调度器。大块GUI/Core栈及UI对象仍在PSRAM，XiP和小对象优先PSRAM策略保留。没有修改微雪引脚、屏幕驱动、桌面资源或.reference。

真实主机CMake生成与实际ThreadConfig::apply回归通过，验证源选择、包装移除、内部/外部栈、名字/亲和性、禁用继承和上游替换点变化失败；现有真实LVGL页面恢复、截图、最近任务、主题、指针手势回归通过。实际ESP构建规则已确认无pthread包装且替代源生效，服务内部栈和XiP实际配置正确。此前DLL错误在正常权限执行中消失，属于隔离运行环境限制；不要建议用户重装SDK。详细分析、修正和实机验收边界见docs/12-startup-thread-memory.md。本轮无LittleFS资源变更，后续只更新主程序，不自动烧录、不新增Git提交、不将未验收稳定性记入COMPLETED.md。

最终ESP-IDF6.1完整构建通过，主程序0x77d7d0，SHA256 286A95209859AE84E9993A89AA5DA23C6A47399B7E9188F1F090E10745917134，ELF 9B48DADC7DC34A9DAA33FBDDB09248A24DA472816845A2E3403FE4783BAF937C。LittleFS181个文件逐字节核对通过。用户明确“我来烧录就好了”，由用户只更新0x10000主程序，不烧录LittleFS、不操作设备串口；实机稳定性仍待验收。

### 2026-10-15 暂停USB主机控制服务作隔离对照

用户后续日志确认9b48dadc7仍异常：Core1中断返回读取内部RAM栈失败，另一次USB后台取锁读取内部RAM信号量字段失败。上一轮线程/锁调整未解决根因，不能认定PSRAM栈或USB mutex就是唯一原因。最新故障发生于读取主题/语言之后，尚无桌面启动完成标记；不能认定桌面切换动画是原因。

对照上游及微雪确认USB后台为当前Brookesia主机控制/上传/BPK安装的1ms周期RX pump，旧微雪Phone未开启；当前UART后端连接状态恒true。用户明确尚未下载BPK，要求先关闭此服务，问题定位好以后再考虑启用。已同步关闭USB自动注册及Core USB bridge，写入enable-device-config.py并扩展已有检查，避免重配自动开启。保留普通串口日志和ROM烧录、USB源码、其余线程/驱动/资源。详细记录见docs/12-startup-thread-memory.md。由用户烧录，不自动操作串口或烧录；本轮无LittleFS变更。

### 2026-10-15 当前稳定基线与下一步 BPK 工作

用户自行构建烧录后提供附件765896a0-e4f7-4239-9f00-fe2ee3777953（ELF前缀83e961082），明确确认桌面正常、反复重启均一次成功。日志完整到达Shell/Core启动与READY，无新增panic；setup后内部free257555、largest212980。已将这项有条件的实机验收写入COMPLETED.md。

当前USB自动注册和Core USB bridge仍关闭，BPK接收/安装服务尚不可用；READY中的BPK installer是固定文字，不代表服务开启。下一步用户要安装BPK，必须先排查并恢复板端主机服务。关闭服务后稳定是重要隔离证据，但具体并发/对象生命周期或内存破坏根因仍未证实，不能只开回两个开关就宣布修复。排查依据、恢复步骤与实机验收范围见docs/12-startup-thread-memory.md末节。

用户要求将当前代码与问题记录提交本地Git，作为恢复USB前的稳定基线；不推送。后续由用户构建、烧录，助手改完代码即停止，不自动构建、不操作串口、不复制产物到build/bootloader/bin。历史阶段的待验收和不提交描述由本节当前状态取代。

### 2026-10-15 USB恢复代码已实施，待用户编译烧录

用户授权实施恢复方案。代码已同步打开USB自动注册和Core bridge；产品显式defer_usb_start，桌面start和reveal成功后才绑定USB。移除1ms主调度器pump，16KiB内部栈RX任务等待数据（50ms超时），24KiB内部栈作业任务阻塞等待安装/call；协议状态单任务拥有，对外有锁快照，abort走队列。安装在SystemApp任务域串行执行，提交期间busy，临时文件保留至回调返回。Core先排空USB再销毁应用/GUI，等待退出后释放队列和驱动。上传会话仅静音串口，设备日志捕获保留。

现有静态集成检查通过，未进行固件编译、串口操作、烧录或BPK安装验收。新增40KiB内部任务栈须随实机日志观察，不将历史根因宣称已证实。完整实施状态、停止等待限制和验收顺序见docs/13-usb-service-recovery.md末节。设置副标题提交fe11938，分析文档提交28a57d4；本轮恢复代码保留工作区供用户编译。

### 2026-10-15 音乐安装验收与内置测试旋律（待用户烧录）

用户授权通过 COM10 安装音乐 BPK。停止占用串口的 ESP-IDF 监视器后，使用不切换 DTR/RTS 的连接成功安装 net.0use.music 1.0.0（24287字节，SHA256 bbec46c9e71f755b8a77a38b40537a438b27419e3b4e93e89a21dbdb114e2d41）；SystemCore 列表确认 Installed，StartApp 后确认 Running、last_error 为空。串口已关闭，未恢复监视器，声音和触摸未验收。最初普通 CLI 连接曾超时，随后捕获一次启动阶段 Core1 Load access fault，匹配 ELF ac22f6e31 解码到 tlsf_walk_pool / heap_caps_get_largest_free_block；不能把此次安装成功作为启动稳定性已解决的证据。

用户随后要求内置测试音乐并由自己烧录。新增原创六秒、16kHz/单声道/PCM16 的 pocket-test.wav，随 pocket_platform 的 EMBED_FILES 链入主程序。Core 挂载后，platform 初始化仅在内部音乐文件缺失时经临时文件写入及关闭成功后 rename；存在则保留，失败警告并在下次启动重试，不自动播放或改音量。用户只更新主程序即可保留刚装好的 Music，进入“内部”点击测试曲播放。该轮不操作串口、不编译固件、不烧录，不把尚未听音的结果加入完成清单。

音频检查通过：192044字节，96000个采样，非静音且无削波，首尾留静音；SHA256 6b11daca65b52a0d2089aaea9925122d014b07f608cd3832e9ad43d040b6351d。嵌入符号与挂载后初始化顺序、现有静态集成检查及差异空白检查通过。以当前已有主程序大小估算，加入音频后factory分区还余327988字节（未计新代码/对齐，最终以用户构建结果为准）。

### 2026-10-15 资源监视独立BPK与原生只读统计（待用户编译烧录）

用户要求参照官方完成资源监视软件。已新增 net.0use.resources 1.0.0：总览/内存/存储/系统四页、中英文/明暗主题、双核CPU、60秒趋势、内部/PSRAM堆、Flash/分区/镜像/LittleFS/SD、频率/运行时间/复位原因。Pocket新增ResourceSnapshot/ResourceStorage；官方每核idle计数配ESP Timer与64位运行统计，首次/长间隔预热不伪装0%。采样请求驱动且有缓存，无新增后台任务；最大连续块5秒、文件存储10秒，SD音频占用时暂缓容量扫描。配置脚本同步defaults与sdkconfig，使用真实IDF Kconfig独立副本核对已生效；原build文件不是本轮编译产物。

Node实际应用/资源回归、现有Music/Logs回归、静态集成/中文字体检查通过；BPK由源码打包。没有编译固件、串口操作、烧录或Git提交，不记入COMPLETED.md。首次由用户只更新主程序，然后安装资源监视BPK即可，无需重烧LittleFS；后续UI改动可单独更新BPK。实现/口径/实机待验收范围见docs/14-resource-monitor.md，安装见examples/resource-monitor/README.md。

### 2026-10-15 用户要求单独记录RAM访问异常

已将安装音乐前捕获的ac22f6e31故障原始关键行、寄存器、当时完整匹配ELF哈希及解析结果写入docs/15-core1-ram-access-fault.md。报错为Core1 Load access fault，MEPC4857daa4，MTVAL4ff771a4；实际lw读取内部RAM堆块字段，发生于堆统计，不等同内存不足或已证实堆损坏。现有build ELF已被替换为937b7f9c...，不能用它解码旧地址；临时两份音乐日志均0字节，原始证据来自会话工具输出。同步纠正docs13“不是USB”的过强标题和200MHz完全稳定的表述，保留看门狗警告及基线源码未核实边界。本轮只改记录，没有构建、串口操作或代码修复。

### 2026-10-15 新附件三次启动异常与资源监视无数据

附件a7e3dc3b-0aa9-46f8-80ca-b507b7a66521为三次Core1启动阶段Load access fault，前两次读内部RAM的信号量/队列字段，第三次pthread mutex解引用且日志截断；均未到桌面/USB READY，未证明应用使用时发生故障。ELF937b7f9ce完整匹配，原附件/ELF/map/旧配置头已备份.tools/diagnostics/937b7f9ce并忽略Git。

确认匹配固件无ResourceSnapshot/ResourceStorage后端及CPU运行统计，而当前待编译源码已包含；用户需重新构建更新主程序。用户确认资源监视有按钮但一直等待；同时修复BPK的字符串Promise拒绝丢失、旧Pocket真实路径错误识别及默认无限等待，版本1.0.1，并补对应回归；Pocket成功注册增加资源API启动标记。启动访问异常仍未确定根因，本轮不改CPU/PSRAM/同步策略，不编译/串口/烧录。详细证据与边界见docs/16-usage-log-and-monitor-waiting.md。

### 2026-10-15 Core1异常下一步诊断：补齐异步总线现场

核对官方P4 TRM和本地IDF：总线错误可异步报告，SDK在BUS_ERR置位时用历史LDPC改写MEPC，未同步回滚普通寄存器/MTVAL。因此此前“合法内部RAM读取失败”的说法证据不足，旧MTVAL范围和反汇编只作线索；真正失败的历史访存地址需LDTVAL/STTVAL确认。下一步方案先保持400MHz/PSRAM/XiP/服务不变，在Core1异常入口保存MEXSTATUS、原始PC及全部load/store历史PC/地址，配合UART core dump，再按总线、权限或对象破坏证据分流。详见docs/17-core1-fault-diagnostic-plan.md；同步补充docs15/16解释边界。本轮只分析与记录，未实施诊断代码、编译、串口或烧录。

### 2026-10-15 用户授权实施Core1故障诊断 v1

已增加产品侧CMake覆盖，在异常入口、SDK使能缓存与改写MEPC前保存每核首次现场；728字节固定内部DRAM保存错误类别、原始PC、历史失败PC/实际访存地址以及PMP/PMA权限。后续标准panic打印阶段输出OuseFault v1。同步启用IRAM panic、UART内部DRAM core dump与4096字节内部dump栈，逐48字节转储进度喂SDK看门狗，避免完整转储超过原10秒时限；不禁用看门狗。保持CPU400/PSRAM200/XiP/USB配置，不改SDK安装目录或分区/LittleFS。真实主机CMake和实际捕获/打印/分块写入回归、隔离Kconfig解析及设备静态集成检查通过。由用户构建、烧录，助手未编译ESP固件、串口操作或Git提交。此为诊断而非根因修复，构建标记、日志保存及实机验证见docs/17-core1-fault-diagnostic-plan.md末节。

### 2026-10-15 修复用户编译遇到的ThreadProfiler计数宽度冲突

启用FreeRTOS U64统计后，上游ThreadProfiler仍把uint32_t*传给uxTaskGetSystemState导致编译失败。新增产品源覆盖，以configRUN_TIME_COUNTER_TYPE本地变量接收后显式转换为旧32位公开样本，保留ABI与短窗口模差值语义；Pocket资源监视继续完整64位计数。旧分析器百分比中间运算扩展为64位。真实CMake与实际采样/快照代码的32/64位主机回归、回绕/长运行/大中间值/零窗口及原错误反向复现通过，不改.reference。设备配置检查允许IDF省略不可见CPU_CLK=false项，仍强制ESP_TIMER/U64。由用户再次构建烧录，本轮无ESP固件构建或串口操作，详见docs/14-resource-monitor.md兼容节。

### 2026-10-15 完整dump与微雪对照，随后延迟无线启动（待实机）

已从用户原生Temp找回两组校验通过的完整dump，与f253686e5 ELF/map/config及用户附件一并归档到`.tools/diagnostics/f253686e5`。Core1在pocket_logs→LittleFS→Flash中转分配→TLSF块头读取时BUS_ERR；Core0在调度器前填充Timer新栈时STORE BUS_ERR。10449/444个识别出的分配块首尾canary完整，日志栈仍有1484字节A5，尚未锁定根因。特别更正：本机IDF6.1的CAPTURE_DRAM会捕获PSRAM已用堆块，并非仅内部RAM，故UART转储可达数MB。

用户以出厂固件为参照。微雪仓库旧bin为IDF5.5.2、版本上限199，不能自动等同用户v3.2板实际出厂映像；其完整源码/config未公开。源码Phone为0.6 beta2、缓存256KiB/128B，当前0.8系列+IDF6.1、缓存128KiB/64B；另当前Hosted constructor在调度器前初始化SDMMC并建6任务。对照证据和边界见docs/18-waveshare-startup-comparison.md。

用户提出无线等系统启动后初始化，已按桌面就绪后启动实现：产品CMake移除Hosted自动ctor/dtor源而不写官方组件；关闭Wi-Fi/SNTP自动注册；桌面/USB流程完成后，通过临时16KiB内部RAM任务显式初始化Hosted，再加入Wi-Fi/SNTP并恢复保存开关/AP/时区；Settings与Shell补齐晚到服务恢复。CPU400、PSRAM200、XiP和缓存参数保留用于时序隔离。Hosted内部ESP_ERROR_CHECK/assert未改，不能保证所有无线错误只影响网络。构建与实机步骤见docs/19-deferred-wireless-startup.md；不把此次时序调整直接记为BUS_ERR已修复。

### 2026-10-15 用户回家后授权本地构建与应用全面复核

无线启动补齐初次偏好恢复门与Settings保存失败双向回滚顺序，避免旧排队动作覆盖最新开关意图。音乐1.1.1修复错误字符串、后台首次曲库恢复、音量队列与关闭生命周期，并修正双页高密度布局。日志1.0.1修复错误显示、读取单飞、暂停跨前后台与关闭后排队刷新；后端补齐并发初始化/启动、创建失败重试、格式化与UTF-8边界。资源监视1.0.1未发现新确定性缺陷，新增完整实际C++后端主机回归，未盲目改统计公式。

首次完整构建超8MiB应用分区，map比对确认最大增长为20秒测试WAV（640044B）；保留20秒旋律改8kHz PCM16为320044B，不改分区或用户音频播放能力。最终ESP-IDF6.1构建通过，主程序0x7dabc0，余152640B约1.82%；BIN SHA256 69804a933ecf5cba4f693a4ac38d830e7ec5fc52779d10c14d42c9ecf60ac4cf，ELF SHA256 9294266bfb385cbc01c94c6e01991e69b8ad9ed18578fa4bfcdfa084db9685ee。保留标准build产物，未复制bootloader/bin、烧录或占用串口。

音乐/日志/资源三个BPK已重打并逐条目对照源码；真实JS、C++后端、LVGL多密度主题语言、中文字体、安装模拟及最终LittleFS181文件校验均通过。仅更新主程序可保留现有用户文件，再装音乐1.1.1/日志1.0.1；实机冷/软重启、真实声音/SD/触摸仍待用户复测。完整检查、hash和限制见docs/20-local-build-and-app-review.md，不记为随机BUS_ERR已根治。
