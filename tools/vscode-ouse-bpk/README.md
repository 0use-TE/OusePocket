# Ouse BPK

在 Cursor 或 VS Code 的活动栏打开 **BPK**。插件扫描工作区里的 Ouse Core 应用（同一目录包含 `manifest.json`、`page.json`、`app.js`），可改 ID、名称和图标，然后打包、串口安装或卸载。

打包、安装、卸载和同步都调用仓库根目录的 `tools/bpk.py`，协议是当前固件的 OPK1。不烧录系统固件。点刷新会短连串口拉一次已安装列表，装完/卸完也会更新状态。设备画面、安装、卸载、同步同一时刻只能占一个串口，开画面时要先关掉再调试。侧边栏可选常用波特率，也可手填自定义值（1200～12000000），选择保存在当前工作区。只能卸载 `user.*` 应用。

可安装应用的 ID 使用 `user.*`，清单为 `runtime=ouse-core`、`api=1`，不使用 `format` 字段。`api` 是 JS 运行时 API 契约，与 BPK 的版号无关。应用名称本地化和自定义图标样例在 `apps/resource-monitor`、`apps/api-test`、`examples/counter`、`examples/calculator`。这些应用不编进固件，系统内置应用仅有设置。

可以编辑默认名称、`zh_CN` 中文名称、`en` 英文名称，未配置语言回退默认名称。图标支持源目录里的 `icon.png` 或 `icon.bin`，推荐 48×48，最大 96×96；自带应用和示例显式使用预转换的 `icon.bin`，同时保留 PNG 设计源。清单上限 8192 字节，最终包最多 524288 字节，包含清单、页面、脚本、二进制图标四个固定文件，以及清单明确声明的最多 16 个资源。页面和脚本各最多 32768 字节。完整规范见仓库 `docs/bpk.md`。

## 图片、文本和二进制资源

清单可添加资源，编辑 JSON 后侧边栏会检查声明和实际文件：

```json
"assets": [
  {"id":"cover", "type":"image", "path":"assets/images/cover.png"},
  {"id":"words", "type":"text", "path":"assets/i18n/zh_CN.json"},
  {"id":"sample", "type":"binary", "path":"assets/sample.dat"}
]
```

每项仅允许 id/type/path。ID 匹配 `[A-Za-z][A-Za-z0-9_-]{0,30}` 且唯一；路径是 `assets/` 下最多 95 字节的唯一 ASCII 相对路径，每段仅字母、数字、`.`、`_`、`-`，禁止 `.`/`..`、空段、反斜杠和冒号。包中不包含目录条目或未声明文件。

image 为 OIC1 BGRA8888，宽、高各最多 512，所有资源图片的像素合计最多 128 KiB，不含启动器图标。源 PNG 打包时自动转换为同目录同名 `.bin`，并改写包内清单；转换后名称冲突会报错。text 为无 NUL 的 UTF-8，每项最多 16 KiB；binary 每项最多 64 KiB；两者累计最多 128 KiB，允许空文件。预转换图片不需要 Pillow。

固件内通过 `Ouse.assets.list()`、`readText(id)`、`readBytes(id)` 同步访问已载入资源；readBytes 返回独立 ArrayBuffer，readText 只接受 text。页面 image 的 src 为资源 ID；`Ouse.ui.setImage(nodeId,imageId)` 切换已声明图片，正常返回 undefined。

私有数据通过异步 `Ouse.storage.get/set/remove/keys/clear/info` 访问，与当前应用 ID 隔离。每应用 32 个 key、单值 8192 个序列化 UTF-8 字节、累计 64 KiB。set 使用标准 JSON.stringify：NaN/Infinity 变 null，嵌套 undefined 按标准转换；顶层不可序列化值、循环、BigInt 会 reject。普通关闭/同 ID 更新保留数据，卸载删除；挂起时原生 I/O 继续，JS 回调等恢复或申请后台运行后投递。`Ouse.system.storage()` 同步读取系统存储缓存，物理文件系统占用与逻辑包/私有数据长度分别统计。完整返回值和错误处理见仓库 `docs/bpk.md`。

## 环境

侧边栏检查三件事：

- `python`：预转换 `icon.bin` 和 OIC1 图片资源打包只用标准库。图标或资源 PNG 转换需要当前 Python 的 Pillow。找不到 Python 时点「选择」指定 `python.exe`，路径记在这个工作区里。
- `tools/bpk.py`：工作区需要是 OusePocket 仓库根目录。
- `pyserial`：串口安装需要。没有时执行 `python -m pip install "pyserial==3.5"`。

生成的包写到仓库的 `.tools/<id>.bpk`。

修改自带应用的 PNG 设计源后，需显式重新转换 `python tools/bpk.py convert-icon apps/resource-monitor/icon.png apps/resource-monitor/icon.bin`，或把清单图标改为 `icon.png` 并使用装有 Pillow 的 Python 打包。不会在缺少 Pillow 时静默复用旧的 `icon.bin`。

## 调试

用本目录作为工作区，按 F5，选择 **Run Ouse BPK**。调试窗口会打开仓库根目录。

## 安装到 Cursor

```powershell
Set-Location tools\vscode-ouse-bpk
npm install
npm run compile
npm run package
```

卡片上的「预览」在编辑区打开一块 320×416 的页面并运行 `app.js`。这是现有 HTML 布局和部分脚本接口预览，不连接板子，字体也不是板上的点阵字；尚未覆盖全部组合控件、包内资源和私有存储 API。检查这些功能使用真机画面入口和设备测试，不能把网页预览结果当作固件验收。

生成 `ouse-bpk-1.0.0.vsix` 后，在扩展页选择 **Install from VSIX**。工作区打开仓库根目录。插件版本保持 1.0.0，不随每次修改递增。
