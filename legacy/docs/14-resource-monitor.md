# 资源监视：只读固件接口与独立 BPK

当前包 `net.0use.resources` 1.0.1，源码位于 `examples/resource-monitor/src`。
本次由用户编译、烧录主程序，再安装 BPK；不修改 LittleFS 分区或系统资源。
应用可独立升级，之后界面与逻辑变更不需要重编译系统。

## 原生数据来源

`components/pocket_platform/pocket_resources.cpp` 提供两个 Pocket Request 操作：

| 操作 | 内容 | 采样策略 |
| --- | --- | --- |
| ResourceSnapshot | CPU、内部堆、PSRAM 堆、系统信息 | 请求驱动，最多每秒更新一次；最大连续块最多每五秒更新 |
| ResourceStorage | 物理 Flash、分区、固件镜像、LittleFS、已挂载 SD | 应用在总览/存储页每十秒查询，手动刷新可立即查询 |

标准返回 `{success:true,data:{ok:true,schemaVersion:1,...}}`，各容量单位均为字节。
Snapshot 包含 capturedAtMs、uptimeMs、sampleDurationUs、cpu、memory、system。
Storage 包含 capturedAtMs、sampleDurationUs、flash、firmware、internal、sd。
无法取得的数据明确标为 null 或 available=false；不以 0% 假装有效采样。

CPU 使用官方 `ulTaskGetIdleRunTimeCounterForCore` 的前后差值和 ESP Timer 时间差：
单核忙碌率 = `100 × (1 - Δidle / Δtime)`，总负载为各核平均，范围0–100。
原生接口计数只在任务切换时记账，因此这是近似负载，短窗口边界可能抖动；显示值限于0–100。
不使用开机累计百分比代替当前窗口，不按频率推算负载，不遍历全任务表。
首次采样和间隔超过五秒的返回重新建立基线；CPU不可用时返回null和error。
原生非Amazon-SMP后端使用此接口，其余RTOS配置明确降级，不访问私有TCB。

配置脚本同时维护 defaults 和已有 sdkconfig，启用 GENERATE_RUN_TIME_STATS、
USE_TRACE_FACILITY、USE_STATS_FORMATTING_FUNCTIONS、ESP_TIMER、U64，关闭CPU_CLK/U32。
格式化选项由官方 Kconfig 的运行统计选项选中；实现本身不调用 vTaskList/vTaskGetRunTimeStats。
64位时间计数避免32位微秒计数约71分钟回绕。没有新增采样任务、空闲hook或永久定时器；
内核的运行时间记账会持续生效，这项开销不因关闭应用而消失。

RAM 使用 INTERNAL|8BIT 与 SPIRAM|8BIT 两个不重叠口径：total/free/used/minimumFree，
largestFree 与 largestAgeMs 是低频快照。它们是可分配堆，不是全部物理SRAM/PSRAM。
不自动执行堆完整性校验、堆转储或日志刷屏；也不声称能修复既有Core1访问故障。

Flash 物理容量用 `esp_flash_get_physical_size`，镜像配置容量单独为configuredBytes。
分区用官方迭代器枚举，固件用正在运行的分区和 `esp_image_get_metadata`；
头信息首次读取后缓存，不每秒验证整份固件或扫描镜像空白。
LittleFS 使用 `esp_littlefs_info`，SD使用 `esp_vfs_fat_info`。
存储查询在 files_mutex 下执行，遇到文件复制持锁立即返回可重试错误。
SD已有媒体读取租约时，返回mounted=true/available=false/busy=true，暂缓可能扫描FAT的容量查询。
没有SD时不主动挂载，不格式化、不更改音量/频率/电源设置。

## 应用与生命周期

JSON采用官方flow/flex、label、progressBar；bindings使用运行时存储键hidden/value/text。
四页为总览/内存/存储/系统，中英文随系统，色彩采用现有主题，不引入图表库。
每秒异步读取，所有请求经同一inFlight门闩串行；只更新可见页和变化的数据。
三十根原生柱形控件显示最近六十秒CPU趋势，每柱两秒均值，最多保留六十个样本。
首次null不进入均值，同capturedAtMs的原生缓存响应不重复累积。
暂停保留快照，手动刷新一次；后台停定时器，关闭释放服务。
generation拒绝晚回调，pause/resume不会放开仍未结束的请求。
旧固件不支持操作时提示更新并停止无效定时重试。

## 验证与边界

已用IDF6.1真实Kconfig解析独立配置副本，确认统计选项及ESP Timer/U64实际保留。
真实JS/JSON的Node回归覆盖声明、路径/绑定、四页、中英文、CPU空值/非法值、错误恢复、
旧接口降级、串行请求、暂停后台、关页晚回调、时间戳去重、趋势上限、SD占用与复位原因。
现有Music/Logs回归及静态配置/资源检查通过，中文字符在现有字体内。
安装包按实际源码打包并逐项核对，代码/资源测试不等于固件编译、真实LVGL渲染或实机负载精度验收。
初始1.0.0 BPK为10个文件、13343字节，SHA256：
`ec383ada2120e175562b93d76973998f36c0bde14325a55fecf03badacba0998`。
本轮不编译固件、不占用串口、不烧录、不Git提交，不写入实机完成清单。

实机检查由用户完成：空闲/播放音乐时的两核负载、前后台资源回落、暂停继续、SD繁忙状态、
LittleFS容量、重启原因及超过71分钟的计数持续性；负载估算不能替代故障现场的core dump。

1.0.1补充真实原生字符串拒绝、旧Pocket的路径错误及5000ms原生请求超时。
超时后暂停自动刷新，可手动重试，避免未完成请求令界面无限等待；未通过JS超时器提前释放请求锁。
这版BPK为10个文件、13726字节，SHA256为
`57dd72e6fddb62075284bae6c9301099f197670f45ed55730f263832097130c6`。
对应实际日志、旧固件缺失监控接口的证据与验证边界见[后续分析](16-usage-log-and-monitor-waiting.md)。

### 用户编译反馈：Brookesia旧线程分析器与64位FreeRTOS统计兼容

用户在thread_profiler.cpp:266编译失败：其公开SampleResult.runtime为uint32_t，
而启用U64后uxTaskGetSystemState需要configRUN_TIME_COUNTER_TYPE（uint64_t）指针。
原代码不兼容此配置，前期配置/JS检查未覆盖该C++调用。

产品CMake新增thread_profiler_runtime.cmake，在构建目录生成兼容源，不修改.reference或公共头。
API先写入本地configRUN_TIME_COUNTER_TYPE，再显式转换为上游32位公开样本格式，避免错误指针类型或越界写入。
旧分析器保留32位模差值语义，采样窗口应小于2^32计数周期；Pocket资源监视独立使用完整64位idle计数，不受该格式限制。
同时把旧分析器CPU百分比的乘法和除数中间值扩展为64位，避免较长采样窗口溢出。

tools/test-thread-profiler-runtime.py以实际sample_tasks/take_snapshot代码和原公开结构，在主机分别验证32/64位：
精确指针类型、低32位回绕、长运行后采样、百分比乘法/除数溢出边界及零窗口；
真实CMake源选择/属性保留、上游漂移拒绝、上游文件哈希不变均通过，原64位指针错误可反向复现。
静态配置检查也适配了IDF省略不可见CPU_CLK=false选项的正常行为，继续强制ESP_TIMER=y和U64=y。
此轮没有编译ESP固件、串口操作或烧录，用户直接再次构建即可；不需要清理build或更新BPK。

参考：[官方FreeRTOS附加接口](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/freertos_additions.html)、
[官方运行时间差分示例](https://github.com/espressif/esp-idf/blob/v6.1/examples/system/freertos/real_time_stats/main/real_time_stats_example_main.c)、
[官方堆调试与统计](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/heap_debug.html)。
