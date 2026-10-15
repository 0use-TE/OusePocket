# 2026-10-15 微雪原版与当前启动故障的对照

状态：完成源码、配置、二进制描述符和已烧 ELF 的离线对照；没有确认最终根因，没有修改运行固件或宣称修复。
用户确认稳定性参照为买来时的出厂固件／微雪提供的 bin，而非自行编译的示例11。

## 对照对象与身份边界

应区分三份内容：

1. 用户这块 ESP32-P4 v3.2 买来时实际运行的固件。目前没有它的完整备份和构建配置。
2. 官方仓库保存的 `firmware/ESP32-P4-WiFi6-LCD-3in5.bin`。本地文件可直接核对，但不能自动等同于第1份。
3. 官方仓库 `examples/esp-idf/11_esp_brookesia_phone` 源码。其 defaults 可比较，不能当成第1或第2份的有效配置。

参考仓库 origin 为 `https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-3.5.git`，
当前 HEAD `870588e`。本次所比较的源文件没有本地修改；仓库中原已有 `.github` 文件缺失状态，本次没有恢复或改变它们。
官方 [README_ZH.md](../ESP32-P4-WIFI6-Touch-LCD-3.5/README_ZH.md) 的固件段明确说明，
该预编译镜像的源码和构建说明未包含在仓库内。

## 可直接核对的二进制信息

| 项目 | 仓库保存的预编译镜像 | 当前已烧应用 f253686e5 |
| --- | --- | --- |
| SDK | ESP-IDF v5.5.2 | ESP-IDF v6.1 |
| 应用名 | phone_p4_function_ev_board | ouse_brookesia_minimal |
| 编译时间 | 2026-03-20 11:35:26 | 2026-10-15 15:44:00 |
| 镜像头 min/max chip rev full | 1 / 199 | 301 / 399 |
| Flash镜像头 | DIO / 80 MHz / 16 MB | DIO / 80 MHz / 16 MB |

仓库镜像 SHA256：`043d84b20dd755c70392d16e4db2d8c54917d83b90e8920713d116a8f6349957`。
Phone app descriptor 位于组合镜像偏移 `0x20020`；另有 `xiaozhi` app，descriptor 在 `0x620020`，同为v5.5.2。
其应用和 bootloader 头的版本上限199，不覆盖当前已确认的v3.2。
这只能说明仓库这个旧制品不是已确认适配当前芯片的参照，不能推断用户刷错过固件，也不能据此否定用户出厂时的稳定体验。

当前 ELF 完整 SHA256 为
`f253686e56f43c174664aa5835eba076dd72dccac38f93c9b8c38214b641e48d`，
与完整dump note一致，已归档于 `.tools/diagnostics/f253686e5/`。

## 源码配置和架构差异

| 项目 | 微雪Phone示例源码声明 | 当前f253实际构建 | 调查意义 |
| --- | --- | --- | --- |
| Brookesia | 单体Core 0.6.0-beta2、Phone | Core 0.8.4、Super 0.8.3、分拆服务、JSON GUI、JS runtime、BPK | 属于框架迁移，启动和线程模型不同 |
| SDK维护范围 | 示例CI为5.5.5与6.0.2 | 6.1 | 当前组合不是原版同SDK的增量修改 |
| L2容量 | defaults明确256 KiB | 128 KiB | 改变内部RAM划分和访存行为 |
| L2 cache line | defaults明确128 B | 64 B | 单独隔离验证，不直接认定非法 |
| PSRAM | 200 MHz | 200 MHz | 不是当前独有设置 |
| XiP | 启用 | 指令、rodata均启用 | 不是当前独有设置 |
| CPU频率 | 未在示例defaults指定 | 400 MHz | 不能由旧源码defaults推断实际出厂频率；400为rev3支持选项 |
| FreeRTOS tick | 1000 Hz | 1000 Hz | 没有该项差异 |
| 编译优化 | PERF | PERF | 没有该项差异 |
| malloc偏好 | 示例defaults未指定 | 普通malloc阈值0，内部保留128 KiB | 分配位置不同，配置由IDF实现 |
| 任务栈 | 旧Phone/LVGL模型 | setup、Core、GUI和Hosted等PSRAM栈；服务主调度等保留内部 | 增加外部栈和Flash操作的组合，需要实机验证 |
| 存储和日志 | 本地UI，未在main挂载SD或启动日志落盘 | LittleFS、SD、后台日志、包管理 | 后期Core1故障可由此路径触发，但不能解释全部早期故障 |

来源：微雪示例11的 `sdkconfig.defaults:15-42`、`main/main.cpp:24-118`、
`components/brookesia_core/idf_component.yml:1`；当前保存的 `sdkconfig.h` 和依赖清单。
示例08也声明256 KiB/128 B、PSRAM200/XiP，但其优化为DEBUG，不能混同示例11。
**本表微雪列不是预编译factory bin的有效配置，CPU、缓存等实际出厂值仍未知。**

## 与最早故障时间吻合的新增路径：Hosted全局构造

当前匹配ELF的 `.init_array` 确实包含 `esp_hosted_host_init`（地址 `0x48120226`）。
Hosted 2.12.3 的 `port_esp_hosted_host_init.c:17-20` 使用constructor，在调度器启动前调用 `esp_hosted_init()`。
调用链为：

```
SDK CORE初始化、堆和PSRAM加入分配器
 -> __libc_init_array
 -> esp_hosted_host_init
 -> esp_hosted_init
 -> setup_transport / rpc_init
 -> SDMMC host/slot、GPIO、queues、mempool，以及4个SDIO和2个RPC任务
 -> SDK SECONDARY初始化
 -> 创建main
 -> vTaskStartScheduler创建Idle0、Idle1、Timer
 -> Timer新栈memset时出现Core0 Store BUS_ERR
```

Hosted任务配置为PSRAM栈、内部TCB。此时任务尚未运行，不应声称已经发生C6握手、SDIO收发或DMA传输。
PSRAM在constructor之前已初始化并加入堆，因此也没有发现“未初始化PSRAM就创建外部栈”的确定错误。
微雪Phone示例关闭services、没有Hosted依赖；其显示等硬件操作从 `app_main()` 后开始。
这是目前确认发生在最早Core0故障之前的一个重要新增硬件初始化路径，值得优先隔离；它本身不是根因证明。

当前也有产品／Brookesia插件注册等C++全局构造，会分配map/mutex等对象。
显示、音频、电池插件注册仅保存factory，不会当场执行板级on_init；本次未找到其中明确的越界。

## 已审计的产品兼容层

通过匹配ELF的DWARF确认，pthread初始化、scheduler等待、profiler计数宽度适配均已烧入。
不能仅用map里的对象文件名猜测覆盖源未生效。

- `pthread_memory_policy.cmake`：去掉Brookesia私有pthread mutex包装，使用IDF原生mutex；
  `ThreadConfig::apply` 从默认值初始化配置并显式关闭inherit_cfg，避免未初始化字段。
  未发现全局malloc/free/operator new劫持。
- `scheduler_poll_wait.cmake`：工作线程空闲等待改成至少一tick的FreeRTOS delay。
  这段代码只有任务开始运行后才会执行，不能直接解释Timer创建前的故障。
- `thread_profiler_runtime.cmake`：修正FreeRTOS64位计数写入旧公共32位字段的兼容问题。
  f253中profiler采样条件不满足，sample_tasks实体未链接，并未在调度器前采样。
- 故障快照和UART喂狗只在异常后运行；它们有静态内存／布局开销，未发现其在正常启动主动写CSR或遍历堆。
- setup、板级显示／音频初始化、日志捕获与落盘、USB安装服务均在app_main之后。
  内部128 KiB保留池也在main_task开始后才建立，故都不能单独解释最早Core0错误。

## SDK和LibC迁移风险

f253 ELF `.comment` 为 GCC esp-15.2.0_20251204 / 15.2.0，配置启用Picolibc及Newlib compatibility。
仓库旧factory bin包含Newlib组件和工具链源码路径。IDF6.0迁移文档指出，
按Newlib头构建且操作struct reent内部字段的预编译库，可能与Picolibc兼容模式冲突并破坏任务栈。
这是一项具体兼容性检查方向，但没有证据证明当前某个库满足触发条件，不能当作当前根因。

## bootloader身份检查的边界

设备日志中的bootloader编译时间为09:35:13，本地现有bootloader为15:44:58，不能直接将本地bootloader配置等同于板上旧制品。
但这也不是已证实的配置错配：当前Flash应用在 `cpu_start.c:1000` 必定执行ext_mem_init，
由应用配置调用cache_hal_init，并在 `cache_hal.c:75-76` 重设L2容量／行长和失效缓存。
时间戳不同不能单独解释运行期总线错误，未发现SDK要求这两份配置必须逐字相同的依据。

## 对比后的判断与后续验证顺序

当前是一次SDK、框架、线程和存储架构迁移，不能因为沿用同一个微雪BSP就视作原版等价底层。
启动稳定性问题应优先按迁移／集成回归排查；功能更多并不使随机访存异常成为可接受行为。

完整dump目前确认：日志任务栈仍有1484字节底部A5，Core1识别到10449个分配块首尾canary无异常；
Core0调度器前新Timer栈也在合法范围内失败。结合这些证据，不能将其简单归为RAM容量不足或日志模块本身损坏。

下一轮应保持400 MHz，固定源码，每个实验只引入一个差异：

1. 隔离Hosted的调度器前初始化路径，观察最早Core0错误是否仍复现；不能仅停Wi-Fi服务自动注册，
   因为已链接组件的constructor仍会执行。要验证初始化时机，应显式延后同一Hosted组件或从测试固件真正移除该组件。
2. 分别对照L2容量128/256 KiB、cache line64/128 B，再验证微雪源码的256 KiB/128 B组合。
   改大L2会少128 KiB可分配内部RAM，完整业务固件可能重新遇到容量问题；应在裁剪测试构建中评估，不能直接当作生产修复。
3. 若前两项不足以解释，再比较IDF6.0.2／6.1及Picolibc／Newlib，记录实际解析依赖与工具链。

每个构建分别记录冷启动和软件重启、Guru和看门狗，而不只计一次进入桌面。
此前400MHz变体及200MHz记录的源码／构建身份并不完整，不能代替此轮固定版本对照。
本次没有自动更改产品配置、编译或操作设备。
