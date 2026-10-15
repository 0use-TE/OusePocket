# Ouse 本地改动

来源：`.reference/esp-brookesia/service/system/brookesia_service_usb`，0.8.1。

2026-10-15：日志 ELF `f1bcf6262` 的第二次重启是启动期间的 panic。匹配 ELF 解码：`Usb::Impl::update_connection_state → pthread_mutex_lock_internal → xQueueSemaphoreTake`，故障队列地址 `0x48746cb0` 落在 PSRAM。正常动态 FreeRTOS 队列应分配于内部 RAM，日志无法确定句柄为何失效。

USB 状态锁和发送锁改为提前创建、位于内部 RAM 的 FreeRTOS mutex，避免普通 malloc 分配的 pthread 包装层；每次获取前校验句柄区域，创建后检查堆完整性。USB 协议、UART 传输与安装接口保持原实现。此为针对故障路径的加固，是否消除偶发故障仍须实机重复重启验证；不将其记为已证实根因修复。

2026-10-15 恢复实现（待用户编译及实机验收）：

- 移除共享调度器上的1ms RX pump，改为16KiB内部栈接收任务；UART/Serial-JTAG读取最多等待50ms，空闲时阻塞，超时仅用于停止、会话和作业完成检查。
- 协议状态、transfer及解析器由接收任务独占；外部读取有锁快照，外部AbortTransfer投递队列。返回成功表示取消请求已接受，并非取消已完成。
- 协议abort对调用方request_id回复done，包含空闲时的通配0请求，避免原来空操作不回复或通配请求回复ID不匹配导致CLI等待超时；非法ID返回错误。
- 安装/通用call交给24KiB内部栈作业任务，任务空闲时阻塞等待作业或退出哨兵；只有一个作业可在途，提交期间拒绝竞争命令，保留临时文件直到安装回调返回。
- 明确退出确认；quiesce超时不释放资源，框架停止时等待退出。Core必须在退出应用、GUI和桥之前排空USB，生命周期操作不能从USB桥回调或Core应用回调内部发起。
- 可选板级console静音回调，在本产品中保留日志环形缓冲和落盘，只暂停UART日志输出。协议和CLI保持兼容，UART0仍为115200，串口可用不等于有物理连接检测。
- 本轮没有固件构建、烧录或真机上传验证；40KiB新增任务栈需在用户实机日志中确认内部RAM和栈余量。
