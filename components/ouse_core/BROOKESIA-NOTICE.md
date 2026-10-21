# Brookesia 桌面移植来源

原项目：Espressif ESP-Brookesia，版权和 Apache-2.0 许可见 BROOKESIA-LICENSE.txt。

- private/navigation_gesture.hpp：直接复制归档 native 手势辅助代码。
- ouse_navigation.cpp：复用辅助代码；时钟格式移植自 shell_status.cpp，应用网格公式移植自 shell_app_launcher.cpp（112×112、18 间距、按屏幕计算列/页）。
- ouse_theme.c：将 resource/themes/color/light.json、dark.json 的语义配色转换为编译期共享 LVGL 样式。
- 原归档的桌面控件通过 GUI 绑定层加载模板创建，无法不带绑定层地直接链接整个 ShellApp。新桌面按对应 launcher 模板与 portrait 常量，用 lv_obj/lv_label/lv_button/lv_image 原生构建，未引入 GUI/JS/JSON 运行时。
- 0.4.2 在该移植基础上重写原生布局和手势状态机：参考归档 shell_display.cpp、shell_quick_controls.cpp 的拖动/遮罩/收尾模式，增加共享控件、设置子页、停留进入最近、卡片居中吸附和清除全部。桌面网格已改用 dp/sp 与名称行高计算，早期 portrait 常量不再是当前布局参数。

参考代码原位于 `legacy/components/brookesia_system_super/`，旧目录已于 2026-10-21 从当前源码中移除，可从 Git 提交 `894c6b4` 查看历史。当前组件独立构建，所有需要的源文件均已复制或移植；本说明与 BROOKESIA-LICENSE.txt 继续保留。
