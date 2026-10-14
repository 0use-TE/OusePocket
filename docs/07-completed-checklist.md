# 已完成清单

本文件按版本追加完成记录。只把已有证据的实现和验证记为完成；待办、已知限制单独列出。

## 2026-10-14 · 首版桌面与 Settings 基线

这是项目的首次本地提交基线，尚未定义产品发布版本号。

### 版本与硬件

| 项目 | 当前基线 |
| --- | --- |
| 开发板 | Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 |
| 实物 P4 revision | v3.2，由启动日志确认 |
| ESP-IDF | v6.1 |
| Brookesia | master 固定提交 `e937455b0db1a3e873b1da61d6d13652f3dcc7e2` |
| 默认桌面 / 设置 | System Super 0.8.3 / Settings 0.8.3 |
| System Core | 0.8.4 |
| 微雪 BSP | Registry `waveshare/esp32_p4_wifi6_touch_lcd_3_5` 2.0.2 |
| 显示与触摸 | 320×480 SPI ST7796 / I2C FT6336 |
| 实机日志对应固件 | ELF SHA256 前缀 `9b5e486ab`，由用户编译、烧录 |

### 已实现并有实机证据

- [x] 建立独立最小固件工程 `examples/brookesia-minimal`，使用当前 Brookesia 的默认桌面和内置 Settings。
- [x] 将微雪屏幕、触摸、背光接入 Brookesia HAL；固定注册符号并保留链接，日志确认三个接口均已注册。
- [x] 启动 Brookesia LVGL DisplaySource，匹配屏幕与触摸输出；实际亮屏，页面操作有触摸事件记录。
- [x] 使用 16MB Flash 分区布局：8MB 固件槽、剩余 LittleFS 资源区，另有 NVS 与 PHY 分区。
- [x] 挂载 LittleFS，加载默认桌面及 Settings 的 JSON、字体和主题资源。
- [x] 安装内置 Shell 和 Settings，约8秒打印 `READY: System Super + Settings + BPK installer`。
- [x] 打开、退出 Settings 并返回桌面，日志记录两次完整操作。
- [x] 进入 Wi-Fi、显示、更多、语言、设备信息等 Settings 页面；页面存在不代表对应外设能力已接入。
- [x] 保存深色主题 `dark` 和语言 `zh_CN` 偏好，日志明确确认保存成功。
- [x] 关闭 Wi-Fi 服务自动注册，满足当前先运行离线桌面的目标。
- [x] 新增初始化阶段内存日志及分配失败回调；最新记录初始化结束后内部 RAM 剩余444487字节、最大连续块303104字节，PSRAM剩余24830264字节。

实机证据：用户附件 `3dd54b74-fe50-4a21-a9f6-9a8ced931b0c`。本次启动记录持续到约81秒，没有非法访问、abort 或分配失败；这不是长期稳定性测试结论。

### 已完成代码或电脑端验证，板端验收待续

- [x] 接入 USB 服务及 System Core BPK 安装桥；日志确认 USB 服务启动。电脑向实物上传/安装 BPK 尚未验证。
- [x] 建立独立 JavaScript 计数器应用 `examples/counter`，保持不预装进固件。
- [x] 打包并校验计数器 1.0.0（每次加1）与1.1.0（每次加10）；源码当前保留1.0.0。
- [x] 在电脑模拟器中验证1.0.0的加1交互；1.1.0板端运行及覆盖安装尚未验证。
- [x] 提供固定 Brookesia 源码准备脚本，以及电脑工具依赖声明。

### 已完成文档

- [x] [项目上下文](00-project-context.md)：固定背景、已确认事项与继续工作依据。
- [x] [需求与路线](01-requirements-roadmap.md)。
- [x] [可行性评估](02-feasibility.md)。
- [x] [架构理解手册](03-architecture-guide.md)。
- [x] [最小工程与计数器教程](04-brookesia-minimal-tutorial.md)。
- [x] [System Core 依赖与核心组件](05-brookesia-core-components.md)。
- [x] [Settings 配置指南](06-brookesia-settings-guide.md)。

### 本版已知限制与下一步验收

- [ ] 历史日志曾在创建 libc 锁时分配失败，也曾出现 TLSF Load access fault。已调整内存策略并新增诊断，最新记录未复现；根因与长期稳定性尚未确认。
- [ ] SNTP仍自动注册，但网络连接接口未接入，启动及打开 Settings 时有校时服务绑定失败日志。
- [ ] System Device关闭，重启与Board Info接口缺失。主题/语言已保存，但“立即重启”无法工作；重启后的实际显示待验证。
- [ ] 背光接口初始化成功；滑块改变真实亮度及重启恢复仍待专项验收。
- [ ] 音频、麦克风、摄像头、SD存储、C6 Wi-Fi/BLE尚未接入此最小固件。
- [ ] 实物安装计数器、1.0.0→1.1.0应用更新、重启保留与失败恢复待验证。
- [ ] GPIO工具、USB声卡/网卡/TTL模式、Android伴侣、支付码、云端AI和软件商店属于后续功能。

完整烧录会写入出厂 LittleFS 图像，可能覆盖已安装应用；当前只有一个固件槽，未提供整机OTA。本版应用更新路线是独立BPK，不能与整机固件更新混淆。
