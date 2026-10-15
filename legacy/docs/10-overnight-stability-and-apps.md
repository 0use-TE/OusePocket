# 2026-10-15 桌面、设置稳定性与独立应用

本轮为用户睡前报错的修复记录。已完成代码、主机回归与 ESP-IDF 编译；设备未连接，以下新增内容等待实机验收，不加入根目录的功能完成清单。

## 设置图标、布局与输入乱码

Settings 首页原资源是 JPG，无法带透明背景。本轮生成透明抗锯齿 PNG，复用系统文字色进行图像染色，取消图标底色与卡片边框。保持官方 Settings 组件与绑定路径，没有新建另一套设置应用。

导航行保留左图标、右箭头与标题/摘要两行，调整内边距。自动高度的 grid 行与 stretch 容器在真实 LVGL 夹具中撑大行高；改为适配文字的固定行高，并检查标题和摘要都在所在行内。不同缩放下仍按 dp/sp 计算。

实际配置原来是 `CONFIG_LV_TXT_ENC_ASCII=y`。LVGL 密码圆点是 UTF-8，第二次输入会先遮罩已有密码；ASCII 模式把圆点当多个字符，后续计数和遮罩长度错误，中文 SSID 同样乱码。defaults 与实际 sdkconfig 改为 UTF-8，并加编译期断言，避免以后重新引入 ASCII 配置。

回归直接使用 LVGL 文本框：先复现 ASCII 第二个 8 的错误，再验证 UTF-8 连续 64 个 8、长度上限、删除、显示密码、中英文混输、光标编辑和清空。

## 两次开机过场

**后续完整日志已纠正此前规避配置**：附件 `533fcc87-0458-463b-a96a-b18c6a166122` 使用相同 ELF SHA，明确触发 `spi_flash_disable_interrupts_caches_and_other_cpu` 中的 `esp_task_stack_is_sane_cache_disabled()` 断言，SP 为 `0x4800ed90`（PSRAM）。关闭 XiP 与本项目保留 PSRAM 任务栈不兼容；已恢复 XiP、指令和 rodata 搬移，并添加编译期及静态配置检查。下文的“关闭 XiP”只记录当时的尝试，已撤销，不是当前方案。此前 Store access fault 是否同源尚未证实。

本次附件 `b321ad50-2230-4ee3-843b-0db4ddb65cca` 的日志出现启动 panic 后重新启动。对应旧 ELF SHA256 为 `a3a9aa084a36aba86cea53bce136f30f3652f4feaaf8111eb1e07097550076ef`，与日志一致。

两次 Load access fault 的旧 ELF 解码分别是：

1. `0x480090ca`：`pthread_mutex_lock_internal`，经过 Asio scheduler 的 `task_cleanup`；异常 mutex 指针落到 ELF 的未映射对齐区域。
2. `0x480102bc`：`esp_vfs_select`，读取 `s_lwip_select_ops.0` 所在只读常量地址也 fault。

因此不能只取消动画播放或认定用户重复点击重启。本轮关闭 `SPIRAM_XIP_FROM_PSRAM`、指令和 rodata 的 PSRAM 搬移；代码和常量保留在 Flash，UI 数据及任务栈继续使用 PSRAM。小于 256 字节的普通 malloc 优先内部 RAM，减少锁等小对象放到外部内存的机会。保留此前的轻量堆检测、阶段完整性检查及 reset reason 日志。

这两处故障提示内存访问或对象寿命异常，配置调整是故障规避，尚未确认根因，也未通过设备长期重启测试。PSRAM 的数据路径没有被取消。新固件若仍 panic，应保留完整日志及与新固件匹配的 ELF，继续定位；不能用旧 ELF 解码新地址。

## 独立音乐与日志应用

[音乐安装与用法](../examples/music/README.md)；[日志安装与用法](../examples/log-viewer/README.md)。两者均为独立 JSON + JS BPK，不加入 LittleFS 的出厂应用目录，Counter、Files 也不预装。

音乐原生层增加 MP3/16-bit PCM WAV 流式解码、动态采样率、暂停/继续/停止和文件关闭。软件退到后台时保留播放；最近任务中关闭应用停止播放。SD 文件有简单读取租约，防止文件还打开时卸载；拔卡错误先结束读取并关文件，再卸载。没有扩大到 GPIO/USB 的通用资源管理器。

日志后端位于 Pocket 服务中，启动后自动记录内部轮转文件，查看器负责读取、筛选、暂停、历史和清除确认。实时环形缓冲、持久文件、RPC 响应和界面文本均有上限。

## 验证与烧录

已运行：

- `tools/test-utf8-input.py`：真实 LVGL 输入，旧问题复现与修复验证。
- `tools/test-shell-controls.py`：真实 Brookesia parser/validator、真实 LVGL 字体/布局/软件渲染；桌面、Settings、Music、Logs 的中英文、明暗、75/100/125% 缩放；手势及主题确认分支。
- `tools/test-phone-recents.py`、`tools/test-pocket-navigation.py`：最近任务截图、拖动/释放、卡片关闭、主题、侧滑、缓存页/标题栏前后台恢复。
- `tools/test-runtime-apps.cjs`：实际应用入口、Manifest 和 UI 路径，服务 I/O 替身；覆盖重复加载、无卡、目录缺失、播放控制、后台恢复、定时器、过期回调、日志筛选和清除确认。
- `tools/test-native-services.py`：实际 Playback C++ 和日志环形缓冲；硬件/解码器替身；覆盖缓冲扩容、边界、无进展、格式切换、暂停关闭、读取错误及任务失败后的资源释放。
- `tools/test-log-backend.py`：实际日志回调/RPC/轮转代码、真实主机文件读写；验证 UTF-8、ANSI、游标、溢出、清除、三文件轮转和历史大小上限。
- `tools/check-device-integration.py` 与 ESP-IDF 6.1 完整构建。

主机 UI 夹具使用 TinyTTF 和小型文档适配器，不是设备上的完整 FreeType/显示驱动；模拟解码器测试也不能证明实际 MP3 解码声音。实机需检查连续重启、设置页、Wi-Fi 密码、最近任务、歌曲播放及拔卡，再将通过的功能记入完成清单。

烧录包仍在 `examples/brookesia-minimal/build/bootloader/bin`。本轮涉及内存配置、程序与资源，建议按导出说明更新四文件；已有设备先备份内部用户数据，LittleFS 重烧会覆盖已安装应用及日志。
