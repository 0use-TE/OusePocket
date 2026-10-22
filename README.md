# OusePocket 0.45.2

OusePocket 是运行在 ESP32-P4 上的轻量多媒体应用框架。仓库根目录是 ESP-IDF 工程：原生 C/C++ 提供系统和硬件服务，LVGL 提供桌面与设置，JSON＋JavaScript BPK 提供独立应用。

目标板为微雪 ESP32-P4 WIFI6 Touch LCD 3.5：16 MB Flash、32 MB PSRAM、320×480 ST7796 屏幕、ESP32-C6 无线协处理器，要求 P4 revision ≥3.1。

## 当前版本

- Core **0.45.2**，Native API **47**，JS API **1**。产品版本以 `version.txt` 为准。
- 桌面分页、最近应用、通知中心、快捷面板、横竖屏、锁屏与原生设置。
- BPK 应用的串口/商店/SD 安装、权限、私有存储和后台生命周期。
- 音频、摄像头、相册、录音、Wi-Fi、BLE/HID 等服务按各自接口和权限提供；硬件支持不代表全部外设功能均已验收。
- 共享游戏框架支持 Canvas、精灵、音效、多点触摸、全屏与防误退出；游戏默认限制为 25 FPS。
- 板级显示发送使用三份 PSRAM 快照和两个独立 DMA 缓冲。0.45.1 的防撕裂实现保留，0.45.2 回收 50 KiB 内部 RAM，将界面刷新调度改为 20ms；调度周期不等于物理显示帧率。

最新验证与限制见 [显示发送与防撕裂](docs/0.45.1-bounded-lcd-sender.md)、[内存与刷新节奏](docs/0.45.2-memory-ui-cadence.md)。桌面翻页的最终手感仍以实际使用为准。

## 构建固件

使用 ESP-IDF **6.1.0** 环境，在仓库根目录运行：

```powershell
idf.py build
```

依赖由各组件的 `idf_component.yml` 与 `dependencies.lock` 固定，包括 LVGL 9.5.0、微雪 BSP 2.0.2、ESP LVGL Adapter 0.6.4、QuickJS-NG 0.14.0。不要直接修改 `managed_components/` 中的下载源码；板级调整位于 `boards/`。

应用固件产物为 `build/ouse_core_desktop.bin`，应用区地址为 `0x10000`。首次空白设备还需要对应的 bootloader 与分区表；应用更新应保留 NVS、LittleFS 和用户数据。构建与烧录是不同操作，本文命令不会烧录设备。

## 开发和安装应用

应用目录为 `apps/<name>/`，必备文件为 `manifest.json`、`page.json`、`app.js`、`icon.bin`，附加资源通过 manifest 声明。当前 BPK 使用 ZIP_STORED，包大小上限为 512 KiB。

```powershell
python tools/bpk.py pack apps/resource-monitor dist/bpk/resource-monitor.bpk
python tools/bpk.py --port COM10 --baud 921600 install dist/bpk/resource-monitor.bpk
python tools/bpk.py --port COM10 list
```

串口安装需要 pyserial；将 PNG 转换为包内图片需要 Pillow。同一个串口由一个程序独占，端口以本机实际连接为准。

- [应用清单](APPS.md)
- [应用 API](apps/README.md)
- [BPK 格式与限制](docs/bpk.md)
- [运行时移植与边界](components/ouse_runtime/PORTING.md)
- [系统状态与待办](docs/STATUS.md)
- [原生 ELF 运行时实验](docs/native-elf-runtime.md)：独立 C 程序的加载、执行与合作停止；当前通过有线调试启动。

## 项目目录

| 目录 | 用途 |
| --- | --- |
| `main/` | 启动入口与故障诊断 |
| `boards/waveshare_lcd35/` | 显示、触摸、无线与板级硬件适配 |
| `components/` | 核心、脚本运行时、设备、存储、任务、安装及媒体服务 |
| `apps/` | 独立 BPK 应用与游戏源码 |
| `tools/` | 打包、串口控制、生成器和主机验证 |
| `assets/` | 共享源资源 |
| `cloud/store/` | 配套应用商店服务 |
| `site/` | 项目静态站点 |
| `hardware/` | 硬件设计与结构资料 |
| `docs/` | 当前说明及按版本保留的验证记录 |

`build/`、`managed_components/`、`dist/`、`.tools/` 和 `tmp/` 是忽略的本地产物、依赖或诊断目录，不属于提交的源码。

旧 Brookesia 工程和微雪示例 `legacy/` 已从当前源码移除；已跟踪的旧文件可从 Git 提交 `894c6b4` 查阅。历史文档中的旧路径仅用于记录当时的来源，不是当前构建依赖；未跟踪的原厂下载文件不在 Git 历史内。移植来源和许可证继续保留在 [Brookesia 来源说明](components/ouse_core/BROOKESIA-NOTICE.md)、组件内许可证及 [字体说明](docs/0.4.13-fonts.md)。
