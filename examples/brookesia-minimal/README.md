# Brookesia 最小学习固件

完整步骤、接口解释和代码导读见 [教程](../../docs/04-brookesia-minimal-tutorial.md)。

采用核对当天的 Brookesia master 固定提交，公开 Waveshare BSP 2.0.2，ESP-IDF 6.1。实物P4 v3.2，初版桌面与Settings已运行。新增重启、音频输出、C6 Wi-Fi和DPI设置已构建通过；步骤见 [Settings指南第9节](../../docs/06-brookesia-settings-guide.md#9-当前接入重启声音wi-fi和dpi)。

当前使用本地维护的 System Super，新增全屏下拉快捷面板（亮度、音量、静音、Wi-Fi状态），底部上滑收起。代码和页面资源均需更新，见 [面板说明](../../docs/07-desktop-quick-controls.md)。

最近任务沿用微雪旧 Phone 的截图/暂停/恢复逻辑，提供横向截图卡片、左右切换、上推关闭与全部关闭；侧滑有跟手返回箭头，支持明暗主题，已获用户实机确认。当前取消 Files 预装，只预装桌面和 Settings，见 [使用说明](../../docs/08-files-power-navigation.md)。

这个教学分区只有一个固件槽，不提供整机 OTA；热更新的是 `examples/counter` 的 JavaScript 应用包。重新运行完整 `idf.py flash` 会写入 LittleFS 出厂资源，可能覆盖已安装应用，因此应用更新使用 `brookesia-usb install`。

2026-10-14更新：按用户要求移除当前固件蓝牙功能，保留Wi-Fi、声音和DPI设置；前文蓝牙描述属于历史实现或后续规划。
