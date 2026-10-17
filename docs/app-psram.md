# 应用容量和 PSRAM

2026-10-17：注册容量从 8 提高到 24，设置占 1 个，最多安装 23 个 BPK。保持现有 1200 字节串口协议：23 个最长 47 字节 ID 加逗号和终止符最多 1104 字节。容量提高不改变每实例 512 KiB JS 堆、后台执行和共享服务预算，也不保证所有应用同时驻留；包大小仍受 LittleFS 剩余空间限制。

## 内存策略

- 注册表动态分配到 PSRAM；分配失败向核心启动入口返回错误。
- 桌面和最近应用的静态状态放入外部 BSS，启用 `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`。
- BPK 清单、图标、资源、页面和脚本，以及运行时实例和资源副本，明确使用 PSRAM；大块分配失败返回错误，不再回退内部 RAM。
- QuickJS 使用 `JS_NewRuntime2` 自定义 PSRAM allocator，覆盖 calloc/malloc/realloc/free。每块通过对齐头保存请求大小，usable-size 只返回调用方可写字节数，不能把底层堆的块大小当作可写范围；原有 JS 堆上限及内存统计继续生效。
- UI/JS 任务栈、同步对象、中断和驱动/DMA 内存策略保持原样。不能把所有内部 RAM 都挪走；原配置的普通 malloc PSRAM 优先策略也仍生效。

这些修改保证相应分配的去向，不代表测得了真机内部 RAM 节省量。没有设备时无法验证实际余量、缓存/DMA 行为、性能和长期稳定性。

## 离线验证

`python tools/test-ouse-psram.py` 从生产源码提取注册逻辑和 JS allocator，校验 24 槽满额、越界拒绝、已装 ID 更新准入、PSRAM 分配标志、注入 OOM、realloc 失败保留原数据，并使用真实 QuickJS 执行及验证 512 KiB 上限。ESP 堆由主机 mock 替代，不验证物理内存地址。

`python tools/test-ouse-app-packages.py` 校验实际 BPK 解析、资源边界、JS 编译和页面 schema。`python tools/test-ouse-kernel.py` 校验任务、设备和执行预算状态机。

桌面主机测试 `tools/test-ouse-core.py` 已增加满容量及溢出检查，但当前脚本因缺少 `ouse_tasks.h` include 路径而在编译阶段失败，本轮未宣称桌面测试通过。ESP-IDF 固件构建单独验证生产代码。

本轮上述三个专项/包/内核检查通过，ESP-IDF 6.1 固件构建通过。链接映射显示 `ouse_core.c` 和 `ouse_shell.c` 共 37,084 字节状态进入外部 BSS（含对齐段共 37,088 字节）；这是当前布局大小，不是与旧版本比较的实测节省值。

产物 `build/ouse_core_desktop.bin`，4,989,728 字节，SHA-256 为 `46a5c811da51bff41dc343ca849e2bd9fa1217e062415cbc3c2bccfae24a1fc9`。8 MiB 应用分区剩余约 41%。没有烧录或访问真机。

## 回家真机验收

1. 仅烧录应用区，保留 NVS 和 LittleFS；升级后确认已有应用和数据恢复。
2. 记录冷启动 internal free/largest/minimum 和 PSRAM；依次打开、回桌面和关闭多个应用，观察内存回收及内部最大连续块。
3. 安装超过原先 7 个 BPK，检查列表、桌面分页、打开、更新和卸载；满 23 个后确认新增拒绝、已有包仍可更新。
4. 验证最近应用预览、主题切换、Wi-Fi、USB 音频和音乐/闹钟并行；观察卡顿、花屏、音频断续和堆错误。
5. 反复打开/关闭及更新应用并长时间运行；结合真实内存读数决定是否进一步迁移栈或调整驻留策略。

## 真机发现与修复：安装 END 崩溃

2026-10-17 经 SSH 在设备所在 Windows 电脑直接访问 CH343 COM5，确认串口握手和包上传正常。原固件在安装校验中报告 `CORRUPT HEAP: Bad tail`、`multi_heap_get_allocated_size ... (head != NULL)`，随后崩溃重启。工具重试 END 时上传状态已丢失，因此最终只显示 `ESP_ERR_INVALID_ARG`。先前只保留 OuseUSB/OuseJS 日志的工具隐藏了堆损坏和重启信息。

根因是新 JS allocator 的 usable-size 使用 `heap_caps_get_allocated_size()`。开启 heap poisoning 时，这个查询来自底层块大小扣除固定开销，仍可能大于原请求长度；QuickJS 会把报告的额外字节用于原地字符串拼接等写入，覆盖位于请求末尾的哨兵。

修复为每块保存原请求长度并保证对齐，malloc/calloc/realloc/free 统一管理头部，usable-size 只报告请求长度；增加溢出、非整齐长度、calloc 清零和 realloc 失败保持原块等回归检查。保留 PSRAM 分配和堆哨兵检查。工具保留首尾诊断行并提供原始串口捕获脚本，固件直接返回安装失败阶段。

原固件哈希为本页“离线验证”中的旧记录，不应继续作为修复版使用。修复版应用区 4,990,624 字节，SHA-256 `8ff92792a00fa6cba96c6169f6590ccdd8a7c24a7260ed13bd57684abf640641`，已在同一设备以 `0x10000` 写入并通过哈希校验。

真机通过：资源监视、计算器、计数器、倒计时、秒表、闹钟、接口测试、服务测试、音乐共 9 个 BPK 安装（加设置共 10 个注册项，超过原容量），包括 507,290 字节音乐包；资源监视原位更新；计算器/音乐/资源监视前台打开。三实例时运行时 errors=0、JS 统计堆 423,824 字节，内部堆余量 56,151 字节、最大连续块 30,708 字节，PSRAM 余量 25,247,280 字节。这是特定 Wi-Fi/USB/界面状态的短时快照，不是最大同时运行量保证。

完成一次硬复位后，HELLO 返回 storage=ready，9 个安装包全部恢复，资源监视再次打开成功。完整恢复包和应用区诊断文件已更新为修复版。上述短测不替代长期稳定性、USB 音频及所有应用功能验收。
