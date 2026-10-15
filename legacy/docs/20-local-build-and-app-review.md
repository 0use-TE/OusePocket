# 本地构建与音乐、日志、资源监视检查

2026-10-15。按用户要求完成无线延迟初始化，并检查三个应用与后端后构建固件。
所有实机结论仍需设备验证；本轮未连接串口或烧录，不把启动顺序调整当作随机BUS_ERR根因已确认。

## 本轮修复

- 无线：桌面及USB接收启动流程结束后，临时内部RAM任务初始化Hosted，再注册Wi-Fi/SNTP。
  保留保存的Wi-Fi开关/AP与时区。Settings和状态栏恢复晚到服务，初次恢复与用户动作之间有顺序保护。
  官方依赖源保持原样，产品CMake覆盖仅移除Hosted自动生命周期入口。
- 音乐1.1.1：修复字符串形式的服务异常、音量请求在暂停/关闭时的排队和释放、后台中丢弃首次目录响应后前台不重载。
  音乐双页适应75/100/125%显示密度，封面完整居中，音量滑块端点留出空间。
- 日志1.0.1：错误信息兼容字符串Promise拒绝；实时/历史切换与返回前台合并读取，关闭后不执行排队刷新。
  暂停会丢弃已发请求的旧响应，切后台再返回仍保持暂停，重复启动不泄漏轮询定时器。
- 日志后端：并发初始化只安装一次捕获钩子；写任务启动串行化且创建失败可重试；
  格式化失败和非法UTF-8安全处理；清空后恢复磁盘状态，但缺少写任务仍报告无法持久化。
- 资源监视1.0.1：后端及应用未发现新的确定性错误，保留实现；补充独立的真实C++后端回归。

## 固件体积

首次完整ESP-IDF6.1构建在最终分区校验失败，主程序为`0x828d90`，比8 MiB应用分区多`0x28d90`。
对比已归档f253686e5映像的map，最大增长是20秒内置测试WAV：原192,044字节变640,044字节，增加448,000字节。
因此保留20秒旋律、单声道PCM16，将合成测试音采样率改为播放器已支持的8 kHz，资产变320,044字节。
该旋律的三次谐波低于4 kHz；用户自己的MP3/WAV播放能力不变。分区、CPU/PSRAM频率和XiP保持原配置。

最终ESP-IDF6.1构建通过：主程序`0x7dabc0`（8,235,968字节），8 MiB应用分区剩`0x25440`（152,640字节，约149 KiB/1.82%）。
构建工具按整数显示2%并提示分区接近满，后续增加系统功能需要控制体积；应用BPK使用独立LittleFS数据分区。
使用标准`examples/brookesia-minimal/build/`产物，不复制到`bootloader/bin`。
最终ELF符号核对：显式`esp_hosted_init`保留，原自动`esp_hosted_host_init/deinit`不在最终ELF中。

| 产物 | 字节 | SHA256 |
| --- | ---: | --- |
| `examples/brookesia-minimal/build/ouse_brookesia_minimal.bin` | 8235968 | `69804a933ecf5cba4f693a4ac38d830e7ec5fc52779d10c14d42c9ecf60ac4cf` |
| `examples/brookesia-minimal/build/ouse_brookesia_minimal.elf` | 350454304 | `9294266bfb385cbc01c94c6e01991e69b8ad9ed18578fa4bfcdfa084db9685ee` |
| `examples/music/dist/net.0use.music-1.1.1.bpk` | 27637 | `bfa5ee4cbd98d05a4b940736070e23907cecc8d210b0c6bf97760dca925bc683` |
| `examples/log-viewer/dist/net.0use.logs-1.0.1.bpk` | 7728 | `dc6331b21eb4582f638f8972a9f2f599c5e5ebc08cdf9a83d2a6d93e3b773c44` |
| `examples/resource-monitor/dist/net.0use.resources-1.0.1.bpk` | 13726 | `57dd72e6fddb62075284bae6c9301099f197670f45ed55730f263832097130c6` |

## 已通过的检查

- `test-wireless-startup.py`：真实启动源11个隔离进程场景、任务创建重试、初次偏好门、析构与自动注册守卫。
- `test-hosted-deferred-init.py`：真实CMake/whole-archive验证自动入口被替换、依赖属性与上游源保持、漂移时拒绝配置。
- `test-deferred-network-ui.py`：真实偏好读取、Settings/Shell晚到服务恢复、一次订阅、开关与排队回滚。
- `test-runtime-apps.cjs`和`test-music-lifecycle.cjs`：真实JS、服务调用边界、错误、后台与过期响应。
- `test-log-backend.py`：真实后端文件I/O、24线程初始化、任务创建失败、轮转、清空、重启历史、UTF-8和控制台抑制。
- `test-resource-backend.py`：完整真实资源后端TU，U64/U32/统计不可用、回绕、缓存、后台预热、内存口径、SD音频租约、容量与镜像失败。
- `test-resource-monitor.cjs`：真实资源应用的超时、错误、页面切换、轮询与前后台行为。
- 桌面导航、真实LVGL最近任务截图/输入/卡片生命周期、真实LVGL UTF-8密码和中文输入回归。
- `test-native-services.py`：真实原生播放实现，硬件/解码器替身覆盖边界、格式切换、暂停停止及文件释放。
- `test-shell-controls.py`：真实Brookesia parser/validator和LVGL渲染，音乐双页、资源四页、日志及Settings/Shell，
  覆盖三种密度、中英、明暗；已查看最终125%截图，无封面偏移、滑块越界和文字裁切。
- 三个应用中文常量的字体cmap核验：音乐64、日志49、资源监视176个不同汉字均无缺字，源字体与staging一致。
- 项目专用安装脚本5项模拟串口回归，测试不打开真实串口。
- 最终设备配置检查、`git diff --check`通过；实际LittleFS181文件逐字节匹配staging，Settings/Shell资源匹配源码，未预装可选应用。
- 三个最终BPK每个条目逐字节匹配源码，清单/版本/ZIP完整性通过。

主机测试使用硬件边界替身，不能证明真实扬声器、拔卡、触摸、无线或冷启动稳定性。
日志环固定静态占用约24.5 KiB内部RAM，本轮保留位置与容量；真实开机后的总余量仍需看设备采样。

## 实机重点

只更新主程序可以保留既有LittleFS中的应用、日志和用户文件；再分别安装新的音乐/日志BPK。
内置测试WAV会按原有规则在文件大小变化时更新。资源监视包可沿用1.0.1。
反复冷启动/软件重启，确认先出现DESKTOP READY再初始化Hosted；测试保存开关开/关、自动连接、SNTP和USB安装。
音乐验证后台播放、最大音量、切换内外存、关闭应用后停止；日志验证暂停跨前后台、实时/历史、清空和重启后历史。
若再次panic，保存首个错误及本轮匹配ELF，继续定位；Hosted内部assert/panic行为尚未改造。
