# 桌面下拉快捷面板

这是 OusePocket 在官方 System Super 0.8.3 基础上增加的功能，放在本地组件 `components/brookesia_system_super`。官方参考源码未修改。

## 如何操作

平时桌面只显示状态栏。手指从屏幕顶部向下滑动后，展开全屏快捷面板，顶部保留状态栏，其余区域遮住桌面或当前应用。

面板可直接调节亮度（10–100%）、音量（0–100%）和静音。Wi-Fi 显示连接状态；开关、扫描和配网仍通过 Settings 完成。亮度和音量操作走现有系统服务，与 Settings 控制的是同一套硬件。

面板不会定时消失。从屏幕底部“向上滑动”区域向上滑，或点击关闭按钮，即可收起。在桌面打开就返回桌面，在应用中打开就返回该应用，不结束应用。内部列表可以滚动，拖动滑块不会被识别为应用退出。

## 手势由谁检测

```text
FT6336 → Touch HAL → Display 服务检测触摸轨迹
                    → Shell 判断顶部下滑 → 显示快捷面板
                                        → GUI 文档 → LVGL
```

Display 服务每 20ms 检查触摸快照，使用方向锁和 40ms 松手去抖。Shell 沿用官方顶部边缘及下移距离阈值：本板 320×480 时，顶部约 38px 内开始，下移约 48px 后展开。收起手势只接受底部 24dp 区域开始的上滑，避免与控件滚动冲突。

旧的顶部手势原本只显示状态栏约 1.5 秒；本地桌面改为展开面板，取消该自动隐藏计时。面板使用全屏可点击容器拦截触摸，文字节点不抢按钮点击，底下应用不会收到面板内的点击。

## 修改入口

| 文件 | 职责 |
| --- | --- |
| `components/brookesia_system_super/src/shell_display.cpp` | 订阅触摸手势和 UI 动作，展开、收起规则 |
| `components/brookesia_system_super/src/shell_quick_controls.cpp` | 读取状态，调用亮度、音量、静音服务 |
| `components/brookesia_system_super/resource/shell/screens/overlay.json` | 全屏面板、滑块、按钮及动作绑定 |

## 烧录与验收

这次既有固件代码变化，也有页面资源变化，需要更新 `ouse_brookesia_minimal.bin`（0x10000）和 `littlefs_data.bin`（0x810000）。四个可携带烧录文件统一放在 `examples/brookesia-minimal/build/bootloader/bin`。重写 LittleFS 会覆盖其中已有应用和文件。

2026-10-14：ESP-IDF 6.1 编译通过，固件大小 `0x747710`，8MB 应用分区剩余约 9%；资源和动作连接静态检查通过。尚未烧录实机。

实机依次检查：桌面顶部下拉、Settings 内顶部下拉、底部上滑收起；亮度实际变化；播放测试音时调音量和静音；面板保持展开超过 1.5 秒；深浅主题和 75%/125% 缩放时能滚动并操作所有控件。

2026-10-14 交互反馈：用户确认面板中的亮度、音量可以调整，但滑钮不可见，静音和关闭缺少明显边框。已为两个滑块配置 `partStyles.indicator`（已选值轨道）与 `partStyles.knob`（当前位置滑钮），为两个按钮显式配置 2dp 边框、内边距和按下颜色；沿用当前主题配色。此轮只修改页面资源，重新构建后更新 LittleFS 即可，视觉效果待实机确认。
