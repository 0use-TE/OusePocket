# 2026-10-15 使用日志与资源监视等待状态

用户附件：`a7e3dc3b-0aa9-46f8-80ca-b507b7a66521/已粘贴的文本.txt`。
用户确认资源监视有标题/按钮，但一直显示正在读取/统计中。

## 附件中的实际启动情况

附件共820行，是连续三次启动故障，最后一段截断。没有桌面READY、USB服务READY、
资源监视on_start/采样或音乐播放日志；安装注册日志不是应用已打开的证据。

| 次数 | 附件行号 | 阶段 | 故障点 |
| --- | --- | --- | --- |
| 1 | 160–237 | 约18.2秒，准备桌面字体 | Core1 Load access fault；MEPC4857b25a，MTVAL4ff65c34 |
| 2 | 506–580 | 约5.1秒，Core初始化 | Core1 Load access fault；MEPC4857b1aa，MTVAL4ff7d218 |
| 3 | 816–820 | 约4.8秒，Super初始化 | Core1 Load access fault；MEPC48009ae8；后续寄存器缺失 |

日志ELF前缀937b7f9ce与本地ELF完整匹配：
`937b7f9ce8ea5ee22f00ded6c29fe26876a9e75eac57963c5b0051b6370e1caa`。
芯片v3.2、IDF6.1、CPU400MHz、PSRAM200MHz、XiP开启。

使用匹配ELF解码，打印的MEPC分别对应：

1. `lw a4,0(s0)`，S0=4ff65c34，位于内部RAM；栈也在内部RAM。
   关联Asio select_reactor → pthread_mutex_lock_internal → xQueueSemaphoreTake。
2. `lw a5,64(a0)`，A0=4ff7d1d8，目标4ff7d218仍在内部RAM；当前栈在PSRAM。
   读取队列uxItemSize字段，关联Boost interruption_requested互斥锁。
3. `lw a1,0(a0)`，pthread_mutex_init_if_static/pthread_mutex_lock，RA关联Asio strand入队；
   未提供A0/MTVAL，不能补写实际目标地址。

与ac22版本的TLSF位置不同，共同症状是Core1 Load access fault，前两次MTVAL落在内部RAM范围。
后续核对发现，SDK在异步总线异常时会以历史LDPC改写MEPC，普通寄存器与MTVAL仍为异常入口现场，
不能直接视为历史指令执行时的寄存器组合。当前日志没有MEXSTATUS及LDTVAL/STTVAL，
尚不能确认真正失败的访存目标或错误类别，详见[诊断方案](17-core1-fault-diagnostic-plan.md)。
本次启动仍有约260KB内部空闲堆，没有分配失败、堆损坏/栈溢出断言或task_wdt。
不将其认定为资源监视采样引起，不把最后字体/日志行当成根因；原故障仍未确认修复。
Wi-Fi协处理器版本警告和断线重连独立记录，尚无证据证明造成该访问异常。

## 所烧固件缺少资源查询后端

对匹配的937b ELF检查，均无ResourceSnapshot、ResourceStorage、resource_snapshot、
resource_storage、pocket_resources.cpp标识；已有内置测试曲标识。
对应build/config/sdkconfig.h也无FreeRTOS运行时间统计配置，而当前待编译sdkconfig已经启用。
因此日志中的固件早于本次监控接口实现，不能只重新安装BPK就获得RAM/CPU统计。

旧版Pocket遇到ResourceSnapshot会先走通用文件操作的空Path校验，返回：

```text
Path unavailable (SD missing or protected directory)
```

此前测试只模拟Unknown operation，没有覆盖这一真实旧后端响应。

## 应用侧确认的问题与修正

真实QuickJS在runtime_js/backend.cpp的reject_promise中用JS字符串拒绝原生Promise，
不是所有错误都为Error对象。应用只读error.message会丢失这种错误，原Node替身只抛Error掩盖了该问题。
此外，SystemHostBridge的call_function_async默认timeout_ms=0，不会自动限制未完成请求的等待时间。

修正范围为资源监视BPK：规范字符串/Error消息、明确旧后端升级提示、原生调用超时5000ms，
超时显示明确提示；保持原请求完成/超时后才放开inFlight，不人为释放后再发竞争请求。
版本更新为1.0.1，补充真实旧路径响应、字符串拒绝及超时回归。
原生Pocket注册成功后新增Resource API registered启动标记，便于核对是否更新到含接口的固件。

真实JS后端已经在生命周期/异步完成后执行drain_microtasks；匹配build也链接了该逻辑。
每个native调用由AppContextGuard设置owner。未将Promise没有pump或上下文天然丢失作为已确认根因。
由于附件缺少应用打开后的调用日志，无法从这份文件进一步确认实际等待落在哪一个请求。

## 产物与后续核对

用户自行构建当前主程序并app-flash，保留LittleFS；成功初始化Pocket后应看到：

```text
Resource API registered: ResourceSnapshot / ResourceStorage, schema=1
```

关闭串口监视器后安装资源监视1.0.1，确认总览能够取得RAM/存储快照，CPU首个窗口后更新。
安装包 `examples/resource-monitor/dist/net.0use.resources-1.0.1.bpk` 已生成，10个文件、13726字节，
SHA256为`57dd72e6fddb62075284bae6c9301099f197670f45ed55730f263832097130c6`。
包内文件与src逐字节核对、JS语法、资源监视新增回归、Music/Logs回归、静态集成检查均通过。
这不是对原启动崩溃的修复承诺；若仍自动重启，先保留新的首个panic与匹配ELF。
本轮未编译、未操作串口、未烧录、未Git提交。

为避免下一次用户构建覆盖现场，完整原附件、匹配ELF/map和旧配置头已保存到
`.tools/diagnostics/937b7f9ce/`并核对ELF哈希。该目录已被Git忽略，原日志不会进入提交。

既有历史：[内部RAM异常](15-core1-ram-access-fault.md)、[资源监视设计](14-resource-monitor.md)。
