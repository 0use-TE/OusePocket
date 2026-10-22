# 原生 ELF 运行时实验

实验分支：`codex/native-elf-runtime`。这版增加在运行中的 OusePocket 上加载、执行和卸载独立 C 应用的能力。系统固件接入加载器后，修改示例应用只需重新编译并传输 ELF，不必再次刷写系统。

桌面提供“原生程序”图标：首次打开会自动运行本机程序，界面显示状态、持续更新的整数输出、用时和退出代码，并提供“运行”“停止”按钮。返回桌面或关闭应用会请求停止该界面启动的任务，不会停止其他通道启动的程序。未安装程序时执行固件中附带、独立编译的计数示例；示例会逐步输出 0～100。

当前提供一个可替换的桌面程序位置。通过电脑安装的 ELF 保存在设备内部 LittleFS，重启后仍可从该图标启动。安装不执行代码；验证通过后写入临时文件、同步并原子替换旧程序，失败时保留旧文件。不更改 JS BPK 格式，尚未提供原生游戏画面、触摸、音频或商店安装接口。

## 编译与运行

在 ESP-IDF 6.1 环境、仓库根目录运行。第一次需要构建并更新包含本实验的**应用固件**，保留设备现有 bootloader、分区表、NVS 和 LittleFS。根目录工程依旧用 `idf.py build`；原生程序使用同一构建目录记录的编译器和 P4 指令集配置。

```powershell
python tools/native.py build native-apps/hello/main.c dist/native/hello.elf
python tools/native.py run dist/native/hello.elf --port COM10
python tools/native.py status --port COM10
python tools/native.py stop --port COM10
python tools/native.py install dist/native/hello.elf --port COM10
```

运行需要在设置中开启设备调试。默认通过有线通道将 ELF 直接传入 PSRAM，校验完整长度和 CRC 后启动，不依赖 SD 卡，也不持久保存 ELF。上传中断超过 30 秒、关闭调试或系统开始排空时会回收未提交的上传缓冲。

可加 `--store` 使用已有 CRC 校验文件通道，写入 `/sdcard/Ouse/Downloads/native-demo.elf` 后启动；此模式需要正常挂载且可写的 SD 卡，可用 `--name hello.elf` 改名。`run` 默认观察 15 秒，超时会请求合作停止并返回错误，不能把超时当作停止完成。

示例约运行五秒，计数到 100 后报告 `result: 100`，入口返回 `exitCode: 0`。再次启动会重新分配、加载与重定位，`.bss` 从零开始。停止时示例主动返回并报告 `-1`。

`install` 将程序保存到 `/bpkfs/ouse-native/desktop.elf`，不依赖 SD 卡；完成时状态为 `installed`。之后直接点击桌面图标运行，不需要电脑连接或开启设备调试。再次安装会替换这个位置的程序。内置计数示例随应用固件一起保存，不需要下载。

## 执行与生命周期

1. 申请 `system.native` 所有的 `native:execute` 系统任务，同一时间只允许一个模块。
2. 工作线程使用独立拥有的内存上传副本，或从 `Downloads/*.elf` 读取文件；文件打开期间持有 SD 租约。文件上限 1 MiB，加载映像虚拟地址范围上限 8 MiB。
3. 检查 ELF32、小端、RISC-V、P4 `ilp32f` ABI、入口、表格与字符串边界、加载段、重定位写入位置、ABI 标记及导入符号。
4. 使用固定版本 `espressif/elf_loader 1.3.5`，通过本地分配适配器提供专用的可执行 PSRAM 映射，完成重定位和指令缓存同步。保留系统 PMP 及 PSRAM 预配置保护设置。
5. 在独立任务中调用 `int app_main(int argc, char **argv)`。入口返回后回收加载映像、文件缓冲和工作任务，记录退出码及报告值。

任务使用 12 KiB 内部 RAM 栈；应用文件缓冲和加载映像放在 PSRAM。内存上传提交时复制出工作线程拥有的副本，随后释放传输缓冲，峰值额外使用最多 1 MiB PSRAM。启动本身异步，成功回复任务 ID 表示已接受，不代表 ELF 验证或程序执行已经成功；随后读取状态。

ESP-IDF 6.1 在 P4 revision ≥3.1 上默认将普通 PSRAM 堆设为 RW、不可执行；上游加载器直接返回普通堆地址会在进入代码时触发 Instruction access fault。`ouse_native_memory.c` 通过链接器包装上游分配/释放函数：以 MMU 页对齐分配 PSRAM，再使用 `esp_mmu_map` 为本次映像建立独立的可执行虚拟地址，加载和重定位均使用这一地址。入口返回后先同步脏缓存、解除映射，再释放原始物理页。若缓存同步或解除映射失败，保留这些页并报告失败，拒绝之后的可执行分配直到重启，避免留下指向已回收内存的可执行映射。SDK 和 managed_components 源码不做修改。

`ouse_native_get_api(1)` 提供版本化回调表：日志、单调毫秒计时、停止查询、可中断等待、整数结果报告。头文件为 `components/ouse_native/include/ouse_native_api.h`。每个程序使用一次 `OUSE_NATIVE_APP()` 声明 ABI；只支持通过回调表调用系统，不导出整套 libc 或 ESP-IDF 符号。

v1 不运行 C/C++ 静态构造与析构，不支持 TLS、任意动态库或其他导入。它支持 `R_RISCV_32`、`R_RISCV_RELATIVE` 和 `R_RISCV_JUMP_SLOT` 的受限用法；不接受普通系统固件 ELF 或其他芯片的程序。

## 停止边界

原生模块属于可信代码，与系统共享地址空间，没有 JS 的执行预算和沙箱。模块必须定期调用 `should_stop()`，长时间等待使用 `sleep_ms()`；只能在入口任务调用这些回调，不能自行创建线程，也不能在返回后保留资源、回调或指针。

停止命令只发出取消请求，不强制删除仍在执行模块指令的任务，也不提前释放其代码。系统重启/关机协调器同样请求停止，并等待真实退出；不合作的程序会让正常排空流程保持阻塞。原生越界访问或非法指令仍可能导致整机异常。本版只适合自己编译和审阅过的实验程序。

## 调试协议与状态

| 命令 | 参数 | 作用 |
| --- | --- | --- |
| `DBG_ELFRUN` | `Downloads/name.elf` | 异步启动，返回 `task=<id>` |
| `DBG_ELF` | `-` | 返回十六进制 UTF-8 JSON 状态 |
| `DBG_ELFSTOP` | `-` | 请求合作停止；无活动任务时幂等 |
| `DBG_ELFBEGIN` | `size:crc32hex` | 准备最多 1 MiB 的内存上传 |
| `DBG_ELFDATA` | `offsethex:datahex` | 连续写入，每块最多 512 字节 |
| `DBG_ELFEND` | `-` | 校验完整性并异步启动，返回 `task=<id>` |
| `DBG_ELFEND` | `install` | 验证并保存到内部存储，不运行；返回安装任务 ID |
| `DBG_ELFABORT` | `-` | 回收尚未提交的内存上传 |

状态包含 `active`、`installing`、`phase`、`taskId`、`stopRequested`、`reported`、`result`、`exitCode`、`bytes`、`error`、`elapsedMs`。阶段为 `idle/queued/validating/loading/running/saving/installed/finished/cancelled/failed`。`result` 只有 `reported=true` 才有意义；`exitCode` 仅在入口实际返回后有意义。安装是不可取消的 `package:native_install` 任务，系统排空会等待提交结束。

## 验证

```powershell
python tools/test-native-elf.py
python tools/test-ouse-kernel.py
python tools/test-ouse-reliability.py --checks coordinator
python tools/test-native-device.py --port COM10
python tools/test-native-desktop.py --port COM10
```

文件检查测试直接编译并调用生产 C 验证器，覆盖真实 P4 ELF、44 类损坏文件和 10000 次随机字节变异。协调器测试覆盖原生任务取消、停止确认前阻塞与排空期间拒绝启动。实机测试验证当前固件 ELF 哈希，再检查重复加载、`.bss` 重置、导入和相对重定位、停止、并发拒绝、无效文件/ABI、文件不存在、非零退出、失败后再次运行及堆完整性。实机记录保存在 `.tools/native-device-check/result.json` 及其引用的串口记录目录。

2026-10-22，COM10 实机验收通过：1608 字节的 hello ELF 多次执行均报告 `100`、退出码 `0`，每次约 5.3 秒；一次合作停止完成于约 48ms，报告 `-1`；860 字节的独立错误示例正确记录退出码 `7`。不完整上传、错误 CRC、错误偏移、主动中止及 31 秒无活动后的过期回收均被验证，后续正常程序仍能执行。预热后的检查前后任务数均为 39，PSRAM 空闲分别为 24,354,604 / 24,354,636 字节，堆完整性为 `ok=1`；这些是本次样本，不是全部负载无泄漏的证明。可执行映射日志为 `backing=0x488e0000 alias=0x4a000000 size=65536`，系统内存保护及 PSRAM 预配置保护均保持开启。

默认内存上传和 `tools/native.py run` 已实机通过；SD 持久化路径的写入测试遇到 SDMMC CRC/超时错误，因此不宣称 `--store` 已实机验收。本实验没有格式化或修复 SD 卡。

桌面验收脚本通过实际触摸点击图标和“运行”“停止”按钮，验证返回桌面的取消与其他会话隔离；安装输出 `321` 的独立程序后，尝试无效更新，再重启并点击图标，以区别内部保存的程序与输出 `100` 的内置示例。最后恢复并安装计数示例。记录与截图输出到 `.tools/native-desktop-check/`。

2026-10-22 桌面版已烧录至 COM10，写入哈希与运行固件 ELF 哈希均核对通过。执行 `tools/test-native-desktop.py --skip-reboot`，实际触摸图标启动得到 `100`；运行/停止、返回桌面取消、外部会话隔离均通过。内部安装的测试程序在关闭并重开应用后输出 `321`，无效更新没有覆盖它；随后已恢复并安装输出 `100` 的正常示例，堆检查 `ok=1`。桌面和结果截图已目视检查。此次没有执行硬件重启持久化测试，不能把“重开应用”记录当作重启验收。

构建过程中将原有传递依赖 `esp_sccb_intf` 显式固定为已使用的 `0.0.9`，防止新增 ELF 依赖时组件求解器同时升级摄像头接口组件。
