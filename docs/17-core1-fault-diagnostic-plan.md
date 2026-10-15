# 2026-10-15 Core1 Load access fault 下一步诊断方案

状态：用户随后授权添加代码，已实施下文第一阶段诊断，待用户编译烧录。现有启动故障尚未确认修复。

## 当前日志缺失的关键现场

官方[P4 TRM](https://documentation.espressif.com/esp32-p4_technical_reference_manual_en.pdf)
§1.13.1（印刷页165）说明默认load/store总线错误可以异步报告，CPU发现错误时可能已执行后续指令。
标准mcause/mepc/mtval不足以完整解释此类错误，应同时检查历史指令和实际访存地址CSR。

本地SDK `D:/esp/v6.1/esp-idf` 已核对：

- `components/soc/esp32p4/include/soc/soc_caps.h` 启用SOC_ASYNCHRONOUS_BUS_ERROR_MODE。
- `components/riscv/include/riscv/rv_utils.h` 根据MEXSTATUS bit8读取LDPC/STPC。
- `components/esp_system/port/arch/riscv/panic_arch.c:300` 用该历史PC覆盖frame.mepc，未回滚其余寄存器。
- `components/riscv/vectors.S` 保存的普通寄存器属于进入异常时刻，不保证属于历史失败指令时刻。

所以旧记录中“合法内部RAM读取失败”的解释应收窄为：打印MTVAL位于内部RAM、打印MEPC可解码，
实际历史访存地址尚待确认；也不能由此判定锁对象损坏、PSRAM无关、USB或硬件是根因。

## 第一阶段：只增加现场捕获

保持CPU400MHz、PSRAM200MHz、XiP与服务配置不变。产品侧诊断在发生异常的Core1上，
早于panic_arch_fill_info改写MEPC，保存固定内部DRAM结构：

| 数据 | 用途 |
| --- | --- |
| 原始frame.mepc、mcause、mtval、mstatus和MHINT | 保存异常入口与总线异常模式 |
| MEXSTATUS | bit8 BUS_ERR、bit9 PMP_ERR、bit12 FETCH_ERR；TRM寄存器1.67 |
| LDPC0/1 + LDTVAL0/1 | 历史失败load指令及对应实际地址 |
| STPC0/1/2 + STTVAL0/1/2 | 历史失败store指令及对应实际地址 |

应保留PC valid位原值，即使mcause为load也保存store组。入口不分配内存、不取锁，
先保存再输出，避免现场捕获自身引发嵌套异常。不用shutdown handler充当panic hook。
错误位可能于MRET清除，不能重启后补读或从另一核心读取来排除原因。

本地SDK没有公开稳定的panic注册回调。实施时须核对项目覆盖/链接包装的位置，
不能仅包装较晚的esp_panic_handler便声称保存到了未改写的frame.mepc。
LDTVAL/STTVAL未在本地csr.h命名，若实现数字CSR访问，须按芯片版本再次核对TRM地址表。

可配合[官方UART core dump](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/core_dump.html)，
按需捕获内部DRAM/heap，使用内部dump栈，保留完整转储、匹配ELF/map和配置。
普通PSRAM heap不会被完整转储；需核对PSRAM任务栈支持、串口转储完整性和内部RAM开销。
当前配置为COREDUMP_ENABLE_TO_NONE，现有日志不是core dump。

## 第二阶段：按证据分支

- BUS_ERR置位：先以有效LDPC/LDTVAL、STPC/STTVAL确认真实目标，再决定调查缓存、PSRAM/MSPI、总线访问或供电。
- PMP_ERR置位：核对该核心PMP/PMA配置、真实地址与访问类型；不直接关闭保护作为修复。
- 真实目标或对象内容异常：调查创建/销毁、失效引用、越界写；按阶段放置堆完整性检查，必要时对具体对象设置写监视点。
- 原因位不明确：先确认捕获够早且没有嵌套异常，不直接归因为野指针。

如需更精确的同步异常，可单独实验MHINT.SBE；TRM说明它会增加load/store延迟，
所以这是第二阶段定位工具，改变时序后不再复现不能作为根治证明。
JTAG亦会改变时序；本地SDK在检测到调试器时还采用不同的RAM PMP权限，需避免将调试连接下不复现视为排除权限问题。

## 第三阶段：可比较的隔离和验收

固定同一源码/配置，只比较CPU400与200MHz，统计启动成功、全部panic、全部看门狗及运行时操作。
既有200MHz日志仍有task_wdt，不能直接将降频记为修复。
依第一现场决定是否再分离PSRAM、服务或调度因素，每轮只改一个因素。
若怀疑底层SDK/板卡，使用相同SDK和板子构建最小双核内部RAM/互斥锁程序，保留正常调度让步，
然后逐项加入PSRAM、显示及网络，并与微雪示例的实际频率、SDK、XiP和功能负载比较。

修复验收需覆盖连续冷启动/重启、桌面切换、USB空闲及BPK安装、资源监视、音乐，
同时检查panic、分配失败和看门狗；仅成功进入桌面一次或暂时降频不等于根因已解决。

## 已实施的诊断 v1

- `components/pocket_platform/cmake/p4_fault_diagnostics.cmake` 在构建目录生成
  `panic_handler.c`、`panic_arch.c` 和 `core_dump_uart.c` 的产品副本，不写SDK安装目录。
  替换点缺失/重复时配置失败，保留源文件编译属性和原对象名称，以匹配SDK的noflash链接规则。
- `panicHandler` 和 `xt_unhandled_exception` 的第一条新增调用即捕获现场，早于panic_enable_cache
  和后续frame改写。实现位于 `components/pocket_platform/diagnostics/p4_fault_snapshot.inc`，
  每核保留首次状态，共728字节内部DRAM；嵌套异常不覆盖首次现场。
  捕获过程只读CSR、帧和写静态快照，不写CSR、不遍历堆、不打印、不取锁或创建任务。
- 除错误类别、历史PC/真实地址、原始PC外，还保存全部32项PMP地址、8项PMP配置及16项PMA配置/地址。
  这些权限值按原CSR输出，后续需解码，不能直接把PMP_ADDR_RAW当字节地址。
- 在标准寄存器打印阶段输出 `[OuseFault v1]` 至 `[OuseFault end]`。
  RAW_MEPC是入口原值，标准MEPC仍由SDK按原逻辑处理；LOAD/STORE中的VALID=1且BUS_ERR=1才用于解释总线历史。
  STATE=1表示首次捕获过程中又发生异常，不能将不完整字段视作有效现场。
- `tools/enable-device-config.py` 同步defaults和已有sdkconfig：启用IRAM panic、UART core dump、
  内部DRAM/heap捕获、4096字节内部dump栈、最多64任务，串口监视器保留原始base64。
  未改分区、CPU400MHz、PSRAM200MHz、XiP或USB启动策略。
- UART输出整片内部DRAM可能超过SDK panic的10秒RTC WDT时限；生成的UART写入源
  每输出一个48字节分块前调用SDK自己的esp_panic_handler_feed_wdts，保留其嵌套异常限制。
  看门狗仍启用，若输出卡住没有继续分块仍可复位。编码、校验和与数据格式不变。

主机验证 `python tools/test-fault-diagnostics.py` 通过：真实CMake生成/源选择/属性保留、SDK漂移失败、
首次现场与嵌套/双核隔离、原始PC保留、有效/无效历史项、错误分类、实际UART分块喂狗及base64内容。
SDK原文件哈希保持不变。另在隔离目录使用真实IDF Kconfig确认新增配置有效，原CPU/PSRAM/XiP配置保留；
`python tools/check-device-integration.py` 通过。未编译ESP固件、未串口操作、未烧录、未提交Git。

## 用户操作和下一份日志

1. 用户在ESP-IDF环境构建现有项目，配置输出应有：
   `OusePocket: early P4 fault snapshot v1 + UART DRAM core dump; SDK sources unchanged`。
2. 仅更新主程序，不需要重烧LittleFS、擦除已安装BPK或修改分区。
3. 开启串口日志后重启，确认启动标记：
   `Fault diagnostics v1: early CSR snapshot + UART core dump (internal DRAM)`。
4. 如再次异常，保留从首次Guru到完整 `CORE DUMP END` 和复位的日志，不要在转储期间手动复位。
   115200波特率下日志会明显变长，可能需要约一分钟或更久；若无END也应保留已有内容。
5. 保留本次构建的 `build/ouse_brookesia_minimal.elf`、`.map` 和 `build/config/sdkconfig.h`，
   在再次构建前与日志一起归档。自定义快照不依赖core dump完整成功，但仍必须与匹配ELF解析。

诊断版增加内部RAM/IRAM占用，并会在故障后延长串口输出时间；编译后的大小和实机捕获完整性待用户验证。
它用于定位原故障，尚不构成根因修复。

### 用户首次编译反馈：修正私有喂狗函数声明

用户编译在生成的core_dump_uart.c报esp_panic_handler_feed_wdts隐式声明。
核对IDF6.1发现该函数未声明于panic_internal.h，而由SDK的port/panic_handler.c自行extern声明。
产品覆盖现改为注入与SDK一致的显式extern声明，移除无效的头文件接入。
原主机替身把函数定义放在writer之前，掩盖了声明缺失；已调整为读取生成声明、writer之后才定义替身，
并增加删除声明必须编译失败的反向检查。主机回归通过；未构建ESP固件，用户直接重新构建即可重新生成覆盖源。
