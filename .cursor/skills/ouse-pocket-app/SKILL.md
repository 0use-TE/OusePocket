---
name: ouse-pocket-app
description: >-
  Write or revise OusePocket BPK apps (manifest/page.json/app.js) with the
  owner's UX taste and device pitfalls. Use when creating a new app under
  apps/, editing calculator/notepad/weather/music/dice/explorer-style BPK
  apps, packing/installing .bpk, or when the user asks for a new 应用/小软件.
---

# OusePocket 应用编写

面向本仓库 `apps/<name>/` 的 JSON＋JS BPK。先读现有同类应用再开写；默认参考 `apps/dice`、`apps/notepad`、`apps/calculator`、`apps/weather`。

完整契约见 `docs/bpk.md`、`apps/README.md`、`docs/0.10.0-services.md`。本技能只固化**口味 + 真机踩坑**，不重复整份 API 手册。

## 用户口味（必须遵守）

- **简洁实用**：一屏一事；不要说明小字、脚注、`已更新 xxx`、API 来源行、模式提示条。
- **中文 UI**：标题与主按钮用中文；技术符号可用 ASCII。
- **模式/次要入口用图标**，不要丑的文案大按钮（例如计算器右上角菜单图标 + bottomSheet 选项）。
- **不要滚动**，除非列表本身必须滚（记事本条目、文件列表）。工具页用 `fill` 均分，压进一屏。
- **不要过度「优化」已可用的布局**：用户说好了就停；禁止擅自加底栏留白、放大显示区、改间距「防切割」除非用户明确要求或截图证实。
- **操作要有可见反馈**：点 `sin` 等应在表达式区看到 `sin(30)`，结果区看到数值；禁止算完两边空白。
- **能力边界诚实**：固件做不到的（如长录音、多段落盘）先说清，不要先做假 UI。

## 开工清单

1. 新建 `apps/<id>/`：`manifest.json`、`page.json`、`app.js`、`icon.bin`（可留 `icon.png` 源）。
2. `id` 必须 `user.[a-z0-9._-]{1,42}`；`runtime: ouse-core`，`api: 1`。
3. 需要能力时在 manifest 声明：`network` / `media` / `sdcard` / `alarms` / `background` / `notifications`。
4. `page.json` 顶层 `"units": "dp"`；根节点 `width/height: fill`，主题色用 `$background` `$surface` `$text` `$secondary` `$accent` `$border` `$error`。
5. 每页最多 **128** 节点；导航最多 16 页 / 栈 8。
6. 打包：`python tools/bpk.py pack apps/<name> dist/bpk/<name>.bpk`
7. 安装（不烧固件）：`python tools/bpk.py --port COMx --baud 921600 install dist/bpk/<name>.bpk`（设备侧常为 COM5，921600）。

## 布局与控件坑

| 坑 | 正确做法 |
| --- | --- |
| 应用区约 **320×416 dp**（非满 480） | 先算高度再摆；科学/程序等多行模式用显式小行高 + 主键盘 `height: fill` |
| 运行时按钮：`fill`/`content` 默认 minHeight **40**；**显式 height 以 JSON 为准** | 紧凑行可写 `height: 32`；不要指望 magically 变小却仍写 32 却被旧固件裁切——当前固件已尊重显式高度 |
| `bottomSheet` / `dialog` | 必须带 `width` `height` `padding` `gap`（缺了会 schema 失败） |
| 手势底栏约 36 dp 透明层 | 内容贴底可能被挡；**仅当截图证实**再加底部 spacer，禁止预防性乱加 |
| `scroll: true` | 仅列表；工具键盘页默认不要 scroll |
| 隐藏行 | `style.hidden` + `Ouse.ui.show/hide`；切换模式后主区应仍一屏内 |

## 字体与文案坑（极易出方框）

系统中文字库是 **NotoSansSC subset**，很多 Unicode 运算符/符号**没有**：

- **禁止**当主按钮文案：`−`（U+2212）、`⌫`、`√`、`π` 等 subset 未收录字符  
- **可用 ASCII**：`-` `/` `*` `+` `<` `()` `%`，以及 `sqrt` `x^2` `pi` `sin`  
- subset 里有的（如 `±` `²` `÷` `×`）也不要赌跨字体；工具键优先 ASCII  
- 改系统中文文案后需跑 `tools/generate-core-font.py` 再编固件；**应用自己的字仍受系统字体覆盖限制**

## JS / 服务坑

- 音频：旧 `Ouse.audio.record(1..3)` 最长 3 秒、仅 RAM；Core 0.22.0 的 `Ouse.recorder` 提供 SD WAV 录音、结束收尾、列表、重命名与删除，需 `media` + `files`，见 `docs/0.22.0-media-system.md`。
- 媒体播放：`Ouse.media` + `capabilities.media`；受限 SD 媒体路径需 `files`。Home 可继续播放，显式关闭或脚本故障时内核按实例停止。
- 网络：HTTPS 需联网、正确时间及 CA 验证。Core 0.22.0 开启交叉签名证书链支持，百度 HTTPS 已实测；不要为绕过证书问题关闭验证或降级明文。
- 存储：`Ouse.storage` 每应用 ≤32 key，单值 ≤8192，合计 ≤65536；适合偏好/短列表，不适合音频。
- 异步：audio/wifi/net/files 任务用 `tasks` / Promise；离开页或 `onClose` 停音频、取消任务。
- `setStyle` 可增量改颜色；主题 token 随明暗更新。

## 交互模板（推荐）

**主操作**：大按钮 / 均分键盘，accent 色给主操作。  
**次要设置**：右上角小图标 → `bottomSheet` 选项列表。  
**列表应用**：`scroll` 列表 + 子页编辑；空态等 storage 加载后再画，避免闪一下。  
**持久化**：`Ouse.storage.set/get`，带 `version` 字段。

表达式类应用：

```javascript
// 一元运算要同时画表达式与结果，禁止只改内部状态
Ouse.ui.setText("expr", "sin(" + before + ")");
Ouse.ui.setText("result", shown);
```

未完成输入时结果区可显示 `"0"`，**不要** `setText(result, "")` 导致「啥也看不到」。

## 交付节奏

1. 实现最小可用 UI + 逻辑  
2. pack → 装真机 → **截图或用户确认**  
3. 只改用户点名的问题；勿连带重构  
4. 需要固件行为（按钮 minHeight、录音时长等）才动 `components/` 并烧录 `0x10000`；普通应用只装 BPK  

## 反例（不要做）

- 底部一堆灰色说明、版本号、数据源致谢  
- 为「安全」加 36dp 底白导致键盘被挤坏  
- Unicode 漂亮运算符上真机变方框  
- 假装支持长录音/文件管理写删除（API 没有就说没有）  
- 同屏堆模式切换文案按钮 + 提示标签 + 滚动条  
