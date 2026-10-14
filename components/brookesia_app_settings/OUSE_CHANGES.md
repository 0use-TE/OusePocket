# OusePocket Settings 分支

来源：Espressif esp-brookesia `e937455b0db1a3e873b1da61d6d13652f3dcc7e2` 的 `app/brookesia_app_settings`，保留原许可证。该目录是产品维护的完整组件，不会由 prepare 脚本覆盖。

本分支当前用于ESP-IDF设备固件，新增NVS实现未维护上游PC构建路径。

产品改动：

1. 显示页增加75%、100%、125%逻辑缩放，NVS同步保存，重启应用。
2. 声音页增加测试音入口，经 AudioPlayback 调用微雪ES8311输出。
3. 修正本地依赖路径与组件版本宏；新增NVS构建依赖。

日常直接维护本目录源码。`tools/enable-device-config.py` 同步工程默认配置和已有sdkconfig，不编译固件。

蓝牙功能已按用户要求移除：设置入口、服务依赖和GATT控制代码删除，HAL/NimBLE/Hosted蓝牙配置关闭。Wi-Fi继续使用ESP-Hosted。

音频基线只支持 `tone://test` 测试音，未接入媒体解码与麦克风；不能当作完整音乐播放器。
