# 框架测试应用

此应用通过 BPK 安装，固件不内置它。只使用 `user.framework-test`，不操作音量、网络、主题等用户设置。

- 默认退出前台时暂停计时，重新打开保留上下文。
- “Run in background” 申请后台执行；“Stop background” 结束申请。
- 两个延迟按钮验证后台主动结束、后台导航隔离。
- 开关、复选框、下拉框验证原生控件与脚本事件。
- 底部输入框分别验证文字/数字/密码和缩小/平移/覆盖策略。

实际设备回归：

```powershell
python tools/test-device-framework.py --port COM10 --elf build/ouse_core_desktop.elf --output .tools/diagnostics/device-050-framework --rounds 1 --faults
```

运行器核对真机固件的 ELF 摘要，保留原始串口、操作记录、界面树、截图和报告。结束时删除本次安装的测试应用，保留其他 BPK 和系统设置。若该应用编号已存在，则拒绝覆盖。`--faults` 额外验证一次无限循环只终止应用。

输入法首阶段提供英文、数字和符号；中文拼音和候选词不属于本阶段。
