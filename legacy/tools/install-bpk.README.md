# 项目专用 BPK 安装脚本

设备先正常进入桌面，关闭占用同一串口的串口监视器。在项目根目录执行：

```powershell
python .\tools\install-bpk.py --port COM10 .\examples\music\dist\net.0use.music-1.1.0.bpk
```

替换串口和 `.bpk` 路径即可安装其他应用。同一应用 ID 的安装走系统原有更新流程。
脚本不打包、不烧录系统、不清空 LittleFS、不自动启动应用。

需要当前 Python 环境已安装 `brookesia-usb`；可先检查：

```powershell
python -m brookesia_usb.cli --version
```

缺少依赖时，安装仓库配套官方工具：

```powershell
python -m pip install .\.reference\esp-brookesia\tools\brookesia_usb
```

默认波特率 115200、单次读写超时 10 秒；可用 `--baudrate`、`--timeout` 调整。
串口必须通过 `--port` 明确指定，避免自动探测打开其他设备。

脚本先以未连接状态创建串口，设置 DTR/RTS 为 False，再打开连接。
仅覆盖官方 `UsbClient` 的初始化，复用官方握手重试、分帧上传、SHA-256 校验、
安装结果处理及退出释放会话，不修改 `.reference` 或已安装 Python 包。
适配基于 `brookesia-usb 0.1.1`，升级依赖后应运行 `python tools/test-install-bpk.py`。

此设置避免程序主动切换复位控制线，不保证所有串口驱动/板卡都无瞬态复位，
也不能阻止固件本身发生异常重启。实机应核对安装前后运行时间没有归零。
VS Code BPK 插件的「安装」按钮运行插件内置的这份脚本，不再调用官方 `brookesia_usb.cli install`。命令行也可以按上面的方式单独使用 `tools/install-bpk.py`。
