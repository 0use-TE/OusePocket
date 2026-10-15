# 资源监视 / Resource Monitor

可独立安装的 Brookesia BPK，包名 `net.0use.resources`，版本 `1.0.1`。界面采用官方 JSON GUI 的 label、flow / flex、progressBar 和标准应用样式，支持 320×480 屏幕、中英文、系统明暗主题和缩放。页面可以上下滚动。

1.0.1 修正原生异步调用以字符串返回错误时提示丢失的问题，并识别旧 Pocket 固件实际返回的路径错误，明确提示升级系统。每个资源请求使用原生服务桥的 5 秒超时；超时后显示中英文提示并暂停自动刷新，保留已有快照。点击“刷新”可手动重试，点击“继续”恢复自动刷新。应用只在原生 Promise 完成后释放请求锁，不用额外 JavaScript 超时器启动第二个重叠请求。

## 使用

- 总览：两核 CPU 忙碌率、实际频率、60 秒趋势、可分配的内部 RAM / PSRAM，以及 LittleFS 文件存储。
- 内存：堆已用 / 总量、可用、开机以来最低可用和最大连续空闲块。最大连续块为最多每 5 秒更新一次的低频快照。
- 存储：Flash 物理容量、分区预留、固件镜像大小、LittleFS 文件空间、已挂载 SD 卡空间。分区空余不能当作文件可用空间。应用不会自动挂载或格式化 SD 卡；SD 媒体播放时暂缓容量查询，避免文件系统扫描影响音频。
- 系统：芯片、版本、核心数、CPU 频率、运行时间、复位原因、固件 / ESP-IDF 版本和采样耗时。

前台每秒异步读取资源快照；总览 / 存储页可见时，文件空间最多每 10 秒自动刷新。暂停后保留当前快照，手动刷新可以更新一次；继续恢复定时刷新。切到后台停止请求，关闭时释放定时器与服务。所有统计请求串行，晚到的暂停前回调不会更新界面。

CPU 刚开始没有足够的时间差时显示“统计中”，统计不可用时明确显示不可用，不会以 0% 代替。每根柱取 2 秒内样本的平均值，30 根柱对应最近 60 秒，空缺不代表 0% 负载。最多保留 60 个样本，不保存到 Flash。

RAM 统计表示可动态分配的堆，包含已用、空闲和碎片情况，不是芯片物理内存占用；PSRAM 中用于代码、只读数据的区域及预留区不在此口径。该应用只展示系统总量，不提供不准确的“每个 BPK 占用”。

## 固件与接口

需要带 `ResourceSnapshot` 和 `ResourceStorage` 接口的 Pocket 服务 `1.0.0`。首次必须由用户编译和烧录对应固件，单独安装或更新 BPK 不会增加固件统计接口；已有相同版本 Pocket 但尚无接口的固件也需升级。应用检测到不支持的操作 / 数据版本或旧固件的资源查询路径错误会提示更新固件。后续只改此应用的界面和逻辑时可单独更新 BPK。

通过官方异步服务调用：

```javascript
brookesia.call_function_async(pocket, 'Request', JSON.stringify({
  Request: { Op: 'ResourceSnapshot' } // 或 ResourceStorage
}), 5000);
```

外层标准响应 `{success:true,data:...}`；`data` 具有 `ok:true`、`schemaVersion:1`。Snapshot 返回 `capturedAtMs`、`uptimeMs`、`sampleDurationUs`、`cpu`、`memory`、`system`，Storage 返回 `flash`、`firmware`、`internal` 和 `sd`。内存 / 存储大小均为字节，CPU 负载为 0–100，未取得的数据用 `null` 表示。

## 资源生成与打包

```powershell
python tools/generate-resource-monitor-resources.py
python tools/package-runtime-apps.py
```

生成包：`examples/resource-monitor/dist/net.0use.resources-1.0.1.bpk`。不要将 dist 包、资源文件放进主程序的资源目录；它是独立安装的应用。

第一次使用需更新带统计接口的主程序，在 ESP-IDF 终端执行（由用户操作）：

```powershell
cd C:\Users\ouse\FastProjects\OusePocket\examples\brookesia-minimal
idf.py build
idf.py -p COM10 app-flash
```

随后关闭串口监视器，在项目根目录安装软件：

```powershell
cd C:\Users\ouse\FastProjects\OusePocket
python -m brookesia_usb.cli --port COM10 --baudrate 115200 install .\examples\resource-monitor\dist\net.0use.resources-1.0.1.bpk
```

串口号按本机设备选择。只更新主程序即可保留已安装应用和内部文件；无需重烧 LittleFS。应用不会自行编译或烧录固件。

参考官方 GUI 示例 `gui/brookesia_gui_interface/src/examples/view/progress_bar.cpp`、`interaction/bindings.cpp` 和 Brookesia JavaScript 生命周期 / 服务调用模式。

固件统计依据：[ESP-IDF FreeRTOS 附加接口](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/freertos_additions.html)、[官方运行时间差分示例](https://github.com/espressif/esp-idf/blob/v6.1/examples/system/freertos/real_time_stats/main/real_time_stats_example_main.c)、[堆内存统计](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/heap_debug.html)。

本地验证：`node tools/test-resource-monitor.cjs`、`python tools/check-device-integration.py`。这些检查不等同于 ESP32 编译或实机布局/负载精度验收。
