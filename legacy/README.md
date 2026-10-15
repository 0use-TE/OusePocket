# OusePocket

基于 Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 的触摸手持工具系统。目标是桌面与设置、媒体与文件、GPIO 调试、USB 设备模式、Android 联动，以及云端服务与应用分发。

目前已建立 Brookesia 最小学习固件，实机日志确认默认桌面及 Settings 启动、页面交互和偏好保存；外设接入与长期稳定性继续验证。微雪参考工程保存在独立仓库 `ESP32-P4-WIFI6-Touch-LCD-3.5/`。

每版完成情况与验证边界见 [已完成清单](COMPLETED.md)。

当前代码已新增系统重启、微雪音频输出、C6 Wi-Fi及DPI设置，构建通过，外设功能继续实机验证。接入说明与验收步骤见 [Settings指南第9节](docs/06-brookesia-settings-guide.md#9-当前接入重启声音wi-fi和dpi)。

已增加全屏下拉快捷面板，可调亮度、音量和静音，显示 Wi-Fi 状态；构建通过，交互待实机验证。用法与修改入口见 [桌面快捷面板](docs/07-desktop-quick-controls.md)。

电源设置、侧滑返回和微雪式截图最近任务见 [使用和架构说明](docs/08-files-power-navigation.md)。截图最近任务与手势导航已获用户实机确认。当前只预装桌面和 Settings，Files 已取消预装，独立开发源码保留；不预装计数器。

## 阅读顺序

想从最小示例开始，先读 [Brookesia 入门与热更新计数器](docs/04-brookesia-minimal-tutorial.md)。配套固件位于 `examples/brookesia-minimal/`，动态应用位于 `examples/counter/`。

理解源码体系时读 [System Core 依赖与四个组件](docs/05-brookesia-core-components.md)，设置接入与自定义读 [Settings 配置指南](docs/06-brookesia-settings-guide.md)。

1. [项目上下文](docs/00-project-context.md)：用户目标、确认事项、事实与待验证问题；后续会话从这里恢复。
2. [需求与路线](docs/01-requirements-roadmap.md)：功能清单、阶段与验收条件。
3. [可行性评估](docs/02-feasibility.md)：硬件限制、Brookesia 版本差异、蓝牙/USB/应用商店的实现边界。
4. [架构理解手册](docs/03-architecture-guide.md)：各层职责、调用流程、资源管理与如何掌控开发。

资料核对日期：2026-10-14。文档中的“建议”是工程方案，“已确认”来自资料或代码检查，“待验证”必须通过后续实验确认。

2026-10-14更新：按用户要求移除当前固件蓝牙功能，保留Wi-Fi、声音和DPI设置；前文蓝牙描述属于历史实现或后续规划。
