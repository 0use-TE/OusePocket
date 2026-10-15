# Ouse BPK

在 Cursor 或 VS Code 的活动栏打开 **BPK**。插件扫描当前工作区里的 JavaScript 应用（`src/manifest.json`），检查打包和安装命令，并可以改名称、版本后一键打包、串口安装。

不烧录系统固件。安装前设备需要已经进入桌面，并且串口监视器没有占用该口。波特率固定 115200。安装使用插件内置的 `install-bpk.py`：先把 DTR/RTS 拉低再打开串口，避免官方 CLI 第一次安装时把设备复位到无响应。

## 两种应用

- 目录里有 `brookesia.config.js`：用 `brookesia build` 打包。计数器和文件管理是这种。
- 没有该配置：只把这个应用的 `src/` 打成 `dist/<id>-<version>.bpk`。音乐、日志和资源监视是这种。

## 命令

侧边栏会检查 `node`、`python`、`brookesia` 和 `python -m brookesia_usb.cli`。它们来自当前机器的 PATH；`brookesia` 再找 `tools/host-tools/node_modules/.bin`。四个都找到时这一区默认收起。找不到时点「选择」指定可执行文件，路径记在这个工作区里，优先于 PATH。`brookesia_usb` 选的是另一份 `python.exe`。插件不会自动安装这些工具。

## 调试

在本仓库按 F5，选择 **Run Ouse BPK**。只打开本目录时，运行 **Run Ouse BPK** 即可。

## 安装到 Cursor

```powershell
Set-Location tools\vscode-ouse-bpk
npm install
npm run compile
npm run package
```

生成 `ouse-bpk-0.1.0.vsix` 后，在 Cursor 的扩展页选择 **Install from VSIX**。
