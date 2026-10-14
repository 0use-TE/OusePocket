# 项目上下文

更新日期：2026-10-14。此文件是长期交接入口，不依赖聊天历史。

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
