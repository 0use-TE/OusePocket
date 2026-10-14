# Brookesia 最小学习固件

完整步骤、接口解释和代码导读见 [教程](../../docs/04-brookesia-minimal-tutorial.md)。

采用核对当天的 Brookesia master 固定提交，公开 Waveshare BSP 2.0.2，ESP-IDF 6.1。板级适配仍待实机验证。先确认 P4 芯片 revision，再选择 profile 构建/烧录。

这个教学分区只有一个固件槽，不提供整机 OTA；热更新的是 `examples/counter` 的 JavaScript 应用包。重新运行完整 `idf.py flash` 会写入 LittleFS 出厂资源，可能覆盖已安装应用，因此应用更新使用 `brookesia-usb install`。
