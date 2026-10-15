# USB/BPK 服务恢复分析（2026-10-15）

当前仅做源码分析，不修改 USB 实现或开启配置，不构建、不烧录。稳定基线提交为 `ae19d24`；设置首页副标题提交为 `fe11938`。用户已确认关闭 USB 服务后桌面及重复重启正常，完整历史证据见 [启动问题记录](12-startup-thread-memory.md)。

## 已确认的实现与风险

以下路径均为仓库相对路径。风险描述是源码分析，尚未证明哪一项触发了历史启动异常。

| 位置 | 代码行为 | 对恢复服务的影响 |
| --- | --- | --- |
| `components/brookesia_service_usb/src/service_usb.cpp`，`Impl::start()`、`Usb::Usb()` | USB 使用 Main 共享调度器，以1ms周期执行 RX pump；当前设备主调度器有3个 worker | 无下载时仍轮询、取锁。单个周期任务有防重入保护，但不排斥其他服务调用和启动线程并行访问 |
| 同文件 `Impl::start()` | 先投递周期回调，再设置 Ready 等状态 | 回调可能在启动状态尚未发布完成时运行；应先准备状态，再开放执行 |
| 同文件 `get_status()`、`get_transfer_status()`、`finish_transfer()`、`fail_transfer()`、`update_transfer_progress()` 等 | getter 持有 `mutex_`，部分写入直接修改 `status_` / `transfer_status_`，没有持有同一锁 | 仅锁住读取不能保护并发读写，尤其 `TransferStatus::error` 为字符串。应统一由单一任务维护工作状态，再用短锁发布完整快照 |
| 同文件 `abort_transfer()`、`pump_rx_queue()` | 外部 AbortTransfer 直接修改 `transfer_`、解析器和接收缓冲；pump 也访问这些对象 | 多 worker 下存在并发访问入口，需要把 abort 作为命令交给协议任务串行处理 |
| 同文件 `stop()`；`.reference/esp-brookesia/utils/brookesia_lib_utils/src/task_scheduler.cpp`，`cancel_internal()` | cancel 标记取消、撤销定时器并移除记录；USB 随后直接删除 RX queue、卸载 transport | cancel 不会 join 已开始的回调。需要独立的“回调已退出”确认，再释放资源；不能只在 cancel 后调用 wait_group 就假定安全，因为任务记录已被移除 |
| `.reference/esp-brookesia/service/framework/brookesia_service_manager/src/service/base.cpp`，`stop_internal()` | `on_stop()` 先执行，随后框架才取消共享调度器任务组 | USB 的 `on_stop()` 必须自己保证资源释放前执行者已退出，不能依赖框架稍后的取消 |
| `components/brookesia_service_usb/src/transport_uart.cpp` | `uart_read_bytes(..., 0)` 非阻塞；`is_connected()` 恒为 true；已存在驱动时直接借用 | UART可用不代表主机已连接。不能把恒true当插拔检测，也不能争抢已有驱动的读取者、事件队列或删除借来的驱动 |
| `components/brookesia_system_core/src/system/lifecycle.cpp` | USB在Core初始化期间绑定；安装桥同步调用 `install_runtime_app_package()`；通用call桥同步调用目标服务 | 应明确Core就绪门槛；接收任务不能长期占用共享调度器，也不能等待必须由自己执行的回调 |

已有 `InternalMutex` 使用 `xSemaphoreCreateMutex()`，并断言句柄位于内部RAM；历史日志仍在内部RAM信号量字段读取处fault。因此“改成内部锁”已经尝试过，不能作为新的根治结论。1ms轮询增加执行次数和竞争机会，但合法锁不应因轮询频繁就访问异常；只改大间隔不足以说明修复。

停止竞态可以独立识别，但现有故障日志没有服务停止记录，不能用它直接解释启动时fault。后台空闲时主要执行状态查询与取锁，发生在传输阶段的并发问题也不能倒推为本次空闲启动故障的唯一原因。恢复时仍需检查首次异常前的栈余量、堆完整性及对象地址，排除其他内存破坏。

## 建议的实现顺序

1. **单一协议任务拥有传输状态。** 使用专门的接收/协议任务，不再把每轮UART接收放到共享Main调度器。RX解析、transfer、会话超时和abort均由该任务串行处理；其他任务只能发送命令和读取受保护的状态快照。快照锁内只复制状态，发布事件、写串口、文件操作和安装调用均在锁外完成。
2. **等待数据，设置有限超时。** 专门任务通过有超时的 `uart_read_bytes()` 或驱动事件等待数据，超时用于检查退出命令和会话期限；不要在共享调度器worker里进行阻塞读取。若使用已有驱动，需要先明确唯一RX使用者和事件队列所有权；不为获得事件队列擅自重装console驱动。首次实现可采用有限超时读取，避免扩大驱动改动。
3. **先初始化，后启动；先停止，后销毁。** 准备好队列、状态和Core桥后才启动执行者。停止时拒绝新请求，通知任务退出，收到明确退出确认后再释放队列/驱动/传输对象。等待超时必须报错并保留仍可能被访问的资源，禁止强删正在运行的任务后释放其对象。检查ServiceBase持锁停止的上下文，退出任务不能等待该锁才能发出退出确认。
4. **把可用端口与有效会话分开。** CH343 UART0仍用115200；主机发合法hello后建立会话，超时后释放会话与日志抑制。不能仅凭UART的 `is_connected=true` 激活会话。Core/文件系统准备完成后才允许安装；可先提供用户主动进入的安装模式作隔离验证，通过后再考虑桌面启动后自动开启监听。
5. **隔离耗时安装。** 上传完成、长度与SHA-256校验通过后再提交给安装执行者，明确临时文件所有权和结束通知。安装期间返回忙碌状态并拒绝竞争传输；停止/abort不得删除安装仍在读取的临时文件。通用service call也需要避免阻塞接收与自身执行队列。
6. **协调串口与日志。** 同一COM只能由一个电脑程序占用；固件协议回复和ESP_LOG共用UART，握手后的日志抑制及结束恢复要与现有日志hook配合。`printf`、ROM输出等不会全部经过ESP_LOG hook，不能假定抑制后通道绝对无杂字。首次验证保留现有帧协议、ACK及CLI波特率，避免同时改动两端协议。
7. **测量资源后再调整配置。** 专门任务栈、队列和驱动缓冲均计入内部RAM预算；按实际调用深度检查栈余量与堆完整性。保留现有XiP和内存修正作为对照，避免一次改动PSRAM、时钟、SDK和服务逻辑而失去定位依据。

这里选择有限超时读取/事件等待，是因为官方UART驱动已提供相应能力，并非需要应用每1ms查询才能接收数据。参考 [ESP-IDF 6.1 P4 UART文档](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32p4/api-reference/peripherals/uart.html)。现有FreeRTOS动态对象默认分配内部RAM，见 [ESP-IDF分配规则](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32p4/migration-guides/release-5.x/5.1/system.html)；应先查对象完整性与生命周期，不能重复把内部RAM位置当作充分保障。

## 如何恢复配置与验收

实现完成后才同步恢复 `tools/enable-device-config.py`、`examples/brookesia-minimal/sdkconfig.defaults` 和已有sdkconfig中的：

- `CONFIG_BROOKESIA_SERVICE_USB_ENABLE_AUTO_REGISTER=y`
- `CONFIG_BROOKESIA_SYSTEM_CORE_ENABLE_USB_BRIDGE=y`

同时更新现有配置检查的预期值；启用注册和Core bridge仍需配合上述启动门槛，不能让原来的初始化绑定立即开始接收。当前这两个开关仍保持n。

建议分阶段由用户构建烧录并观察：

1. 仅启动服务且不连接安装工具，反复冷启动/重启、桌面设置切换、空闲运行；记录重启次数、运行时长和匹配ELF。
2. CLI hello/status、会话超时、端口反复打开关闭及服务启停；确认日志恢复、任务/锁/队列无泄漏、无panic。
3. 先安装最小Counter包，确认上传字节数及校验、桌面出现图标、可打开关闭、重启仍可运行。
4. 覆盖同ID更新、传输中断后重试、abort、空间不足、损坏包，以及安装执行阶段的停止处理。传输超时应保留可启动系统并清理可安全清理的临时数据。
5. 再安装Music/Logs等依赖原生服务的包，验证声音、存储、后台与关闭释放；USB上传成功不代表这些功能自动通过。

这是一份待实施方案，不是已完成的修复或实机保证。桌面稳定、服务空闲稳定和真实安装稳定需要分别获得证据。

## 与后续BPK开发的关系

开发JSON界面、JavaScript逻辑和本地打包可以现在开始；USB服务影响的是当前选定的真机上传安装通道。上传、安装、运行、更新、重启扫描这条链路验证后，就可以持续开发并独立更新使用现有能力的BPK，不必每改一个应用都编译系统固件。

当前BPK不携带任意C++硬件驱动。需要新的摄像头、录音、蓝牙或其他尚未提供的能力时，仍要先在固件/HAL/共享服务中实现并开放接口；应用还受内存、文件系统空间、JS运行时和已暴露API限制。SHA-256校验只证明传输一致，不代替包格式/依赖校验或签名策略。

已有Counter适合打通最小闭环，Music/Logs适合随后验证Pocket原生服务。开发用法见 [BPK教程](11-bpk-preview-and-install.md)，官方参考：[Toolkit](https://docs.espressif.com/projects/esp-brookesia/en/latest/system/toolkit.html)、[串口安装工具](https://docs.espressif.com/projects/esp-brookesia/en/latest/system/usb_cli.html)。
