# 独立计数器应用

计数器使用官方 Toolkit `js-gui` 模板组织，采用 JSON UI 与 JavaScript，应用 ID 为 `net.0use.counter`。

本目录独立于 `brookesia-minimal` 固件，不参加固件资源 staging。先烧录默认桌面，之后才单独构建/安装本应用。见 [教程第七节](../../docs/04-brookesia-minimal-tutorial.md)。

```powershell
../../tools/host-tools/node_modules/.bin/brookesia.cmd build
node tools/set-version.mjs 1.1.0 10
../../tools/host-tools/node_modules/.bin/brookesia.cmd build
```

第二次构建产生同 ID 的 1.1.0 包，每次加 10。计数只保存在内存，关闭/更新后重新从 0 开始。
