# 2026-10-15 启动崩溃：线程与同步内存策略

## 已确认的证据

用户日志附件 `a11ecc87-11fa-4c5a-8767-69dadc2a529f` 与本地 ELF 的 SHA256 完全匹配：
`9805e17cd08b14cdcec3e2eca3231fac2ef8a47bc4bdcaddf608d51169f301e7`。
匹配 ELF 和生成配置头保存在 `examples/brookesia-minimal/build/diagnostics/9805e17cd/`。

日志包括两次 Core 1 异常：

- `0x4800f946`：`call_end_selects`，指令 `lbu a4,0(s1)`，读地址 `0x487bc6d8`。
- `0x48065f4e`：`InternalMutex::lock`，指令 `sw ra,12(sp)`，向任务栈保存返回地址；尚未执行锁校验或 `xSemaphoreTake`。

两次均涉及 PSRAM 地址。不能据函数名称断言 USB mutex 损坏，也不能据此证明具体 PSRAM 硬件故障。
系统初始化前内部空闲内存 385863 字节、最大块 253940 字节，本次无分配失败记录。
此前“84 字节内部 RAM 分配失败”与这次访问异常需要分别判断。

## 微雪和官方实现的对照

- 微雪 `11_esp_brookesia_phone` 使用 BSP 初始化屏幕；旧 Phone 默认关闭 Brookesia 服务，不能证明当前新 Core/Super 加 Wi-Fi/USB 的组合稳定性。
- 当前使用微雪 BSP 2.0.2，屏幕绘制缓冲20行，小于 BSP 的50行；不据此更换屏幕驱动。
- ESP-IDF 原生 pthread 默认创建内部 RAM 栈，允许应用通过 `esp_pthread_set_cfg` 明确改变。
- Brookesia `ThreadConfig::apply()` 创建了未初始化的 `esp_pthread_cfg_t new_cfg`，没有设置 `inherit_cfg`。SDK 会读取该字段决定子线程配置继承，属于可独立确认的未初始化读取缺陷；尚未证明它解释了所有访问异常。
- Brookesia 另有全局 `pthread_mutex_init/destroy` 包装，使用 `MALLOC_CAP_DEFAULT|MALLOC_CAP_8BIT` 分配对象及 FreeRTOS mutex，可能落到 PSRAM。PSRAM 对象并非一律不受支持；本轮恢复 SDK 默认实现以减少当前崩溃路径中的外部同步对象依赖。
- 官方 P4 勘误中部分 PSRAM/MSPI 问题只影响旧芯片。用户设备为 v3.2，不直接套用 v3.0 的勘误结论。

参考：

- [ESP-IDF pthread 配置及继承说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/pthread.html#esp-idf-extensions)
- [P4 芯片勘误及适用版本](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/02-errata-summary/index.html)

## 本轮修正

产品 CMake overlay `components/pocket_platform/cmake/pthread_memory_policy.cmake`：

1. 去除全局 pthread mutex 链接包装及其源文件，恢复 SDK 原生 pthread。
2. 为 `thread_config.cpp` 生成本地替代源，先调用 `esp_pthread_get_default_config()`，显式设置 `inherit_cfg=false`，再应用 Brookesia 声明的字段。
3. 对包装选项、源文件数量和替换点进行检查。上游发生不兼容改变时配置失败，避免静默失效。
4. 不修改 `.reference`。

配置同步脚本同时维护 defaults 和实际 sdkconfig：服务主调度器和通用工具线程栈设为内部 RAM。
保留第二调度器显式开启，因为 Pocket/媒体服务会绑定它；不能依赖“主栈在外部才默认开启”的上游规则。
Core/GUI 大栈、普通界面/JSON 对象和 Hosted 驱动支持的外部栈继续使用 PSRAM。
XiP、指令和 rodata 搬移继续开启；小对象内部优先阈值保持0，内部保留池128KiB。

## 验证与边界

`tools/test-pthread-policy.py` 使用真实主机 CMake 验证源选择、链接选项和编译标志，执行生成源中的实际 `ThreadConfig::apply()`：
覆盖内部/外部栈、有无 PSRAM、名字和亲和性、重复调用以及明确禁用继承；替换点缺失/重复时必须失败。
静态集成检查和现有真实 LVGL 最近任务、页面恢复、截图、主题及指针输入回归通过。

本轮只涉及主程序和配置，无 LittleFS 资源修改。实机验证时只更新 `0x10000` 的主程序。
未获得后续烧录许可前不自动烧录，不把构建/主机回归等同于实机稳定性验收。
验收至少检查连续启动、桌面与 Settings 切换、最近任务恢复、主题保存重启，以及 Wi-Fi 初始化期间无 panic/内存分配失败。

此前编译器 DLL 错误来自隔离执行环境；相同本机编译器在正常权限执行环境可启动，不需要修改或重装用户 SDK。

## 本轮最终产物

ESP-IDF 6.1 完整构建通过，主程序大小 `0x77d7d0`，8MiB应用分区剩 `0x82830`（约6%）。
实际LittleFS镜像181个文件与staging逐字节一致，Settings/Shell源码一致；本轮仍无需重烧LittleFS。

- 主程序：`examples/brookesia-minimal/build/ouse_brookesia_minimal.bin`
- 主程序 SHA256：`286a95209859ae84e9993a89aa5da23c6a47399b7e9188f1f090e10745917134`
- ELF SHA256：`9b48dadc7dc34a9daa33fbddb09248a24da472816845a2e3403fe4783baf937c`

用户明确“我来烧录就好了”。不操作串口、不自动烧录；主程序地址 `0x10000`。
代码修正和主机验证已完成，设备启动及桌面稳定性仍待用户烧录后验收，不能宣布所有panic根因已被证明或实机问题已根治。

## 后续：按用户要求暂时关闭主机控制服务

后续附件4059401d-ed03-41f9-b9b7-538de8367b6d确认9b48dadc7版本仍会在Core1中断返回、读取内部RAM任务栈时fault；附件368ecf23-eb4f-493b-9e0f-1213926393ff又记录USB轮询更新状态取锁时读取内部RAM信号量字段失败。两次都不能再归因为“只要把PSRAM栈移到内部就能根治”。最新一份正常日志结束于主题/语言读取，尚无桌面启动完成记录；不能认定是进入桌面的切换动画导致。

源码对照确认，1ms周期的RX pump来自上游Brookesia USB主机控制服务，供电脑命令、文件上传及BPK安装使用。微雪旧Phone例程没有启用该服务。当前UART后端的is_connected恒返回true，状态轮询不具备真实插拔检测能力，但RX pump仍在空闲时持续执行。

用户明确尚未使用BPK下载，要求先关闭服务，根因定位完成后再考虑恢复。本轮仅关闭：

- `BROOKESIA_SERVICE_USB_ENABLE_AUTO_REGISTER=n`
- `BROOKESIA_SYSTEM_CORE_ENABLE_USB_BRIDGE=n`

defaults、实际sdkconfig、配置同步脚本和已有静态检查同时更新。保留UART0 console、正常ROM固件烧录、USB组件源码和依赖，以便之后明确恢复。其余线程/内存配置、微雪驱动、桌面资源不变，作为单一功能隔离对照。本轮不是宣布USB已经被证明为根因；由用户烧录主程序后观察是否仍panic。未自动烧录。

## 当前结论：关闭 USB 服务后的稳定基线（用户已实机确认）

2026-10-15，用户自行构建、烧录后提供附件 `765896a0-e4f7-4239-9f00-fe2ee3777953`，并确认“现在是完全没出问题了，重启都是一次成功”。该日志的 ELF SHA256 前缀为 `83e961082`，不同于上文仍会崩溃的 `9b48dadc7`；日志只提供前缀，不补写未经核实的完整哈希。

日志中 Shell 和 Core 均启动完成，14353ms 到达 READY；setup 后内部 RAM 空闲257555字节、最大连续块212980字节，PSRAM空闲23516372字节。所提供日志没有新的 panic 或异常重启，USB 服务没有启动记录。重复重启成功来自用户实机反馈，不把单份日志当成多次启动的完整记录。

目前两个 USB 开关仍为关闭状态。日志中的 `READY: System Super + Settings + BPK installer` 是既有固定提示，不能据此判断 BPK 接收服务已启用。普通串口日志和 ROM 固件烧录不依赖该服务。

### 待解决问题：恢复板端 USB/BPK 安装服务

- **现象与隔离结果：** 启用主机服务时，即使没有下载 BPK，后台仍执行周期 RX pump；历史故障栈包含 USB 状态更新、取锁和内部 RAM 信号量读取异常。关闭自动注册与 Core bridge 后，用户确认启动恢复稳定。USB 服务路径是当前重点排查对象；具体是同步对象生命周期、并发访问还是其他内存破坏，尚未证实。
- **对照差异：** 微雪旧 Phone 示例没有开启这套 Brookesia 主机服务，不能据其正常运行认定当前 USB 路径无问题。当前 UART 后端 `is_connected()` 恒为 true，RX pump 的1ms定时周期来自上游实现；这些是代码事实，尚不能单独解释访问异常。
- **恢复工作：** 检查服务启动/停止、周期回调的取消与对象生命周期、状态锁和发送锁的并发使用，以及 UART0 与 console 共用时的协议处理。以当前稳定版本作对照，先复现并定位，再恢复 `BROOKESIA_SERVICE_USB_ENABLE_AUTO_REGISTER` 和 `BROOKESIA_SYSTEM_CORE_ENABLE_USB_BRIDGE`；不能把重新开启开关当成问题已修复。
- **验收范围：** 服务开启后的重复启动、桌面与设置切换、空闲无主机、主机连接断开、实际 BPK 上传/校验/安装，以及重启后应用可运行。BPK 安装功能尚未经过本次实机验收。

本次按用户要求本地 Git 提交代码和问题记录，保持 USB 服务关闭。后续构建、串口及烧录由用户操作，不自动执行；不复制产物到 `build/bootloader/bin`。上文构建产物与待验收描述是各历史阶段记录，当前状态以本节为准。

## 后续异常归档：ac22f6e31 的 Core1 RAM读取访问故障

用户随后恢复USB并安装音乐时，在音乐包实际传输前捕获新的启动阶段异常：
`Core 1 panic'ed (Load access fault)`，MEPC=`0x4857daa4`，MTVAL=`0x4ff771a4`。
当时匹配ELF确认是`tlsf_walk_pool`读取内部RAM堆块字段时发生访问异常，尚未证明堆损坏或USB为唯一根因。
原始关键摘录、完整旧ELF哈希、已知400/200MHz对照与证据边界，统一记录在
[Core1内部RAM读取访问异常](15-core1-ram-access-fault.md)。上文USB关闭状态属于历史基线，后续状态见该记录与USB恢复文档。
