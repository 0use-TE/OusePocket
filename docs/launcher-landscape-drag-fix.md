# 横屏桌面长按拖拽修复

桌面重建已按横屏 6×2 / 竖屏 3×4 布局，但 launcher_reposition 与 launcher_drag_point 仍固定 3×4；长按首次移动会改变图标位置，落点行列也不匹配。

launcher_shape 统一为桌面创建、拖拽重排和落点计算提供行列，容量仍为每页 12 个。

本次固件构建通过并写入 COM10 app 区，哈希校验通过；设备报告当前工作区固件 0.42.0。真机验证横屏第一行交换、第二行拖动、越界取消，恢复测试前顺序并退出编辑模式，errors=0，无 panic/重启。截图 .tools/launcher-before.jpg、launcher-during.jpg、launcher-second-row.jpg、launcher-restored.jpg；日志 .tools/launcher-real-test.log 与 .tools/launcher-restore-test.log。

完整主机回归运行到 --orientation 时超时，未声明全套通过；新增的主机触摸检查段已撤下，真机验证独立记录。其他任务已有的源码及版本更新保留。
