# OusePocket 桌面扩展

来源：Espressif ESP-Brookesia `brookesia_system_super` 0.8.3，上游提交 `e937455b0db1a3e873b1da61d6d13652f3dcc7e2`。保留 Apache-2.0 授权；`.reference/esp-brookesia` 保持原样。主工程通过 `override_path` 使用本目录。

2026-10-14：新增全屏下拉快捷面板。

- 桌面顶部原状态栏保持常驻；应用中原状态栏仍按原规则隐藏。
- 从顶部边缘下滑展开，保留状态栏，覆盖其余屏幕并拦截下方应用触摸，不自动收起。
- 通过 Display 服务调整亮度，通过 AudioPlayback 服务调整音量、静音；打开时读取实际值，服务不可用时禁用对应控件。
- Wi-Fi 展示连接状态，配网和开关仍在 Settings 中操作。
- 从底部 24dp 区域向上滑动收起；关闭按钮也可收起。恢复展开前的桌面或应用，不停止应用。
- 切换前台应用时收起面板；面板内滚动、拖动滑块不触发原来的应用退出手势。

入口：`src/shell_display.cpp`、`src/shell_quick_controls.cpp`、`resource/shell/screens/overlay.json`。新增私有依赖 `brookesia_service_audio`。布局沿用官方 GUI 文档和 LVGL 后端，支持现有主题、语言及逻辑缩放。

构建验证和实机验证分开记录。没有增加通知中心、最近任务列表或蓝牙功能。

同日视觉修正：亮度和音量滑块显式设置 indicator/knob 样式，显示已选轨道与当前位置滑钮；静音和关闭按钮增加显式 2dp 边框、内边距、居中文字和按下配色。
