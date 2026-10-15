# 2026-10-15 Core 1 内部 RAM 读取访问异常记录

状态：未确认根因，未标记修复。此记录对应此前安装音乐前捕获的异常，区别于早先84字节内部RAM分配失败。

## 原始错误内容（关键摘录）

来源：本次会话中读取 COM10、115200 串口得到的原始工具输出。以下为其中关键行，非完整串口日志；省略重复回车。

```text
I (16493) SvcSNTP: [service_sntp.cpp:0481] (set_state) : State changed: CheckingNetwork
I (16501) SvcMgr: [base.cpp:1131] (start_internal) : Started service: SNTP
I (16508) SvcMgr: [manager.cpp:0542] (bind) : Service started: SNTP
Guru Meditation Error: Core  1 panic'ed (Load access fault). Exception was unhandled.

Core  1 register dump:
MEPC    : 0x4857daa4  RA      : 0x4857dabc  SP      : 0x4ff82080  GP      : 0x4ff38280
S0/FP   : 0x4ff771a4  S1      : 0x4ff771a0
MSTATUS : 0x00011881  MTVEC   : 0x4ff20003  MCAUSE  : 0x00000005  MTVAL   : 0x4ff771a4
MHARTID : 0x00000001

ELF file SHA256: ac22f6e31

Rebooting...
ESP-ROM:esp32p4-eco7-20260109
Build:Jan  9 2026
rst:0xc (SW_CPU_RESET),boot:0x30f (SPI_FAST_FLASH_BOOT)
```

错误含义：Core 1 执行读取指令时产生访问异常，异常未被处理，panic后自动重启。
这是“读取RAM失败”，不是日志已经报告“RAM不够”。最后输出SNTP不等于SNTP就是触发源。

## 对应固件与已完成定位

异常后同一固件的启动输出：芯片 ESP32-P4 v3.2，ESP-IDF v6.1，
App version `28a57d4-dirty`，Compile time `Oct 15 2026 13:14:04`，CPU400MHz，PSRAM200MHz，XiP开启。

当时本地ELF SHA256已完整核对为：

```text
ac22f6e315723346788ab7ec1c361b297bac01abaa9af3811dc2972b0c2197a5
```

当时使用该匹配ELF做过地址解析与反汇编，确认：

```text
heap_caps_get_largest_free_block()
  -> heap_caps_get_info()
  -> multi_heap_get_info()
  -> multi_heap_get_info_impl()
  -> tlsf_walk_pool()
```

日志打印的MEPC对应指令：

```text
4857daa4: 40cc    lw a1,4(s1)
```

- 用日志中的S1计算，`S1 + 4 = 0x4ff771a4`，恰与MTVAL一致，正常4字节对齐；异步异常下不能据此证明历史指令当时的实际访问地址，见下方补充。
- 地址位于当次启动注册的主内部堆范围 `[0x4ff54730, 0x4ffbafc0)`。
- `SP=0x4ff82080`也在内部RAM范围；真正失败的访存目标仍需历史总线地址CSR确认。
- 静态PMP配置把此数据区设为可读写；没有异常现场完整PMP状态，不能据此排除运行态权限/总线异常。
- 堆遍历为查询最大连续空闲块；官方实现遍历时持有堆锁，不能直接归因为正常malloc与统计之间缺锁。
- 这只能定位异常发生处，不能证明遍历函数本身有错，或先前是哪段代码写坏了内存。

记录时再次读取当前build ELF，哈希已变为：
`937b7f9ce8ea5ee22f00ded6c29fe26876a9e75eac57963c5b0051b6370e1caa`。
它不是本次异常对应的ELF，不得用它重新解释上述旧地址；上述解析结果来自此前确认匹配的ac22版本。

## 与安装/桌面切换的先后关系

这次异常在音乐包真正传输前捕获，当段启动日志尚未出现桌面/USB服务READY标记。
不能把它写成“音乐BPK解包失败”或“点击进入音乐时崩溃”。此前CLI请求超时与这一阶段相邻，
但没有线路波形或完整命令/复位追踪，不能证明串口打开动作直接导致此次panic。

随后保持DTR/RTS不切换的连接中，音乐上传24287字节、SHA256校验及安装成功；
系统列表确认Installed，启动后Running、last_error为空。该成功记录不代表启动故障已根治。
内置测试曲及资源监视代码均在这次异常之后新增，不能倒推为该旧异常的原因。

## 已知对照与未确定部分

此前对照原始文件位于Windows临时目录，不能当作长期归档：

- `ouse-cpu200.log`：10次200MHz启动，均到READY、无Guru，但10次均有非致命task_wdt警告。
- `ouse-final.log`：12次200MHz启动，均到READY、无Guru，其中6次有task_wdt警告。
- `ouse-base.log`：8次400MHz对照，3个窗口到READY且无fault；共记录9次Guru（自动重启可导致一窗口多次）。
  该日志打印App version为28a57d4，不能只凭历史备注认定逐字对应ae19d24源码。

这些记录支持继续查频率/时序相关性。降频也会改变任务竞争、功耗和总线时序，
不能宣布已排除USB/其他软件，也不能宣布芯片或PSRAM硬件损坏；200MHz仍有看门狗警告，不能称完全正常。

原始记录未发现此次异常前的ALLOC FAILED、堆越界断言或栈溢出断言，但日志未报告不等于排除了这些问题。
当前未锁定具体越界写、失效对象、访问保护状态、SDK缺陷或硬件故障。
后续应保留匹配ELF与首个异常现场，再作同代码单变量对照；本次仅记录，不改代码、配置或操作设备。

## 保存范围

临时文件 `ouse-music-install-20261015.log` 和 `ouse-music-launch-20261015.log` 在本次核对均为0字节：
它们是后来空闲读取留下的文件，不包含上述panic。此文从仍保留的会话原始工具输出摘录证据，
不声称已归档完整原始串口文件或保存旧版ELF副本。

历史分析见 [启动线程内存记录](12-startup-thread-memory.md)和[USB恢复与频率对照](13-usb-service-recovery.md)。

后续用户提供937b7f9ce版本完整附件，新增三次启动阶段读取锁/信号量的异常，
并报告资源监视一直等待。已保留该版本ELF/map与原附件，详见
[使用日志与资源监视等待状态](16-usage-log-and-monitor-waiting.md)。两次版本和故障点应分别保存，不用旧ac22地址解释新故障。

## 后续核对：异步异常的解释边界

P4默认支持异步load/store总线错误。当前SDK在MEXSTATUS.BUS_ERR置位时，
用LDPC历史失败指令改写frame.mepc，但不同时回滚普通寄存器或MTVAL。
因此上述反汇编、当前寄存器计算和内部RAM地址范围是线索，不能单独证明
历史指令在失败时访问的就是这个内部RAM对象。旧日志未打印MEXSTATUS及LDTVAL，
无法事后确认当次是否进入这条异步路径。下一步入口快照与单变量实验见
[Core1异常诊断方案](17-core1-fault-diagnostic-plan.md)。
