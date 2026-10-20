# 控件与通知测试应用

这是临时安装的 BPK，不内置于固件。用于检查组合控件、样式合并、状态样式、模态返回/背景关闭，以及通知发布、更新、取消、后台发布和点击唤起。此说明描述测试用途，不代表脚本已经运行通过。

清单同时声明 background 和 notifications 能力。app.js 在顶层绑定事件并初始化 ready=0；首次创建不调用 onResume。已存在实例恢复时 onResume 增加序号，测试据此区分重新创建和恢复。通知监听也在顶层安装，使关闭后由通知启动的新实例可以收到待投递 payload。

控件契约和限额见 `docs/bpk.md`。本应用的 toolbar 与带副标题 listItem 使用系统最小高度，测试应依据实际控件树定位。dialog/bottomSheet 初始隐藏，Back/背景关闭回调中的 isVisible 应为 false；样式 patch 不应重置之前设置的字段。

运行 `tools/test-device-phase2.py` 前须先构建并烧录对应固件。脚本核对 ELF，保存原始串口、操作、界面树、三张截图与结果。仅使用本次拥有的 `user.stage2-test` 和 `user.stage2-other`，若已存在则拒绝覆盖；不改系统设置、不连接网络、不重启设备。

```powershell
python tools/test-device-phase2.py --port COM10 --elf build/ouse_core_desktop.elf --output .tools/diagnostics/device-060-stage2
```

通知清空测试会触发系统的“清除全部”。若设备已有其他应用的普通通知，测试会停止，避免清除用户消息。
