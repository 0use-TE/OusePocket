# UI 开发与后台运行机制

本文说明当前系统**怎么给应用做界面**，以及**后台怎么跑**。完整 API 与配额见 [BPK 契约](bpk.md)；应用目录速查见 [apps/README](../apps/README.md)。

---

## 先回答：浅色发灰要改哪里

| 层 | 职责 | 和「浅色发灰」的关系 |
| --- | --- | --- |
| 主题真源 `components/ouse_core/theme/colors.json` | light/dark 共 12 个语义色 | 改色只改这里，再跑生成脚本 |
| 固件 `ouse_theme.c` + `ouse_theme_palette.inc` | 编译期嵌入色表，供 `$desktop` / `$button` 等 | 旧浅色桌面灰≈按钮灰；现已灰底白键 |
| 应用 `page.json` | 选用哪些 token | 选错会放大问题；按键优先 `$surface` |
| VS Code 预览 | 读同一 JSON（或插件内 `media/theme-colors.json` 副本） | 与固件同源，仍须刷固件才上真机 |

结论：改色改 JSON → `python tools/gen-theme-colors.py` → 刷固件；应用侧用对比度清楚的 token。

---

## 1. 系统如何提供 UI 开发

### 1.1 分工：原生壳 + 可安装 BPK

- **原生（C + LVGL）**：桌面、设置、状态栏、手势条、快捷面板、通知中心、软键盘、应用安装与生命周期调度。
- **BPK（JSON + JS）**：可安装应用。固件不把业务 UI 写死进二进制；包落在 LittleFS，由运行时加载。

一种包格式，四个必备文件，外加清单声明的 `assets/`：

```
manifest.json   # id、名称、图标、capabilities、资源表
page.json       # 布局与静态样式
app.js          # 逻辑：绑事件、改文案、读写存储等
icon.bin        # 桌面图标（可由 icon.png 打包转换）
```

### 1.2 开发到上机的路径

```text
源码目录（manifest / page / app.js / icon）
        │
        ├─ VS Code「Ouse BPK」预览 ──► 电脑 WebView 模拟控件与主题（不跑真 LVGL）
        │
        └─ 打包 BPK（tools/bpk.py 或插件「打包/安装」）
                │
                └─ 串口安装（OPK1，默认 921600）──► 设备 /ouse-apps
                        │
                        └─ 打开应用：解析 page.json → 建 LVGL 控件 → 跑 app.js
```

预览用于改布局和样式；**主题真源是仓库里的 `colors.json`**，固件与插件都从它生成/读取，设备运行时不解析主题 JSON。

### 1.3 页面：声明式布局

`page.json` 描述控件树（dp/sp），不是 HTML。

- 常用：`container` / `card` / `toolbar` / `label` / `button` / `slider` / `textInput` / …
- 尺寸：`width`/`height` 可为数值、`fill`，部分可用 `content`
- 当前板应用内容区约 **320×416 dp**（不含系统状态栏高度）
- 样式：`style` 或运行时 `Ouse.ui.setStyle`
- 颜色：`#RRGGBB` 或系统引用 `$surface`、`$text`、`$accent`、`$button`、`$desktop` …

系统引用会随浅色/深色主题变；写死的 `#ffffff` 不会变。

多页用顶层 `pages` + `initialPage`，JS 里 `Ouse.navigation.push/back`。切页会重建该页控件，**不**触发应用级 `onPause`/`onResume`。

### 1.4 脚本：改数据，不直接碰 LVGL

`app.js` 通过全局 `Ouse` 操作模型；**JS 任务不进 LVGL**。触摸等事件由 UI 任务入队，脚本任务消费后再改模型，UI 任务再刷控件。

典型流程：

1. 首次打开：执行 `app.js` **顶层**（做 `Ouse.ui.on(...)` 等初始化），**不**调用 `onResume`
2. 用户操作 → 事件回调 → `setText` / `setValue` / `setStyle` / `storage` …
3. 回桌面再进来：若实例还在，走 `onResume`；变量与计时器策略见下文「后台」

### 1.5 主题从哪里来

单一真源：[`components/ouse_core/theme/colors.json`](../components/ouse_core/theme/colors.json)。

```text
colors.json          （仓库真源，只改这一份）
    │
    └─ tools/gen-theme-colors.py
            ├─► ouse_theme_palette.inc              固件编译嵌入
            ├─► media/theme-colors.json             VSIX 附带副本
            └─► src/theme-colors.generated.ts       编进扩展 JS（上架/云端也自带）
```

**上架 / 云端安装**：预览只用扩展内嵌色表，不依赖用户工作区有没有固件仓库，不会因找不到 `components/.../colors.json` 崩掉。  
**本地开发本仓库**：若工作区是 OusePocket，预览可直接读 `colors.json`，改色刷新预览即可。

改色后：

```powershell
python tools/gen-theme-colors.py
# 或在插件目录：npm run compile（precompile 会先 sync-theme）
```

再编译刷固件；发扩展前跑 `npm run package`（会先生成再编译）。不必手改 `ouse_theme.c` / `preview.js`。

| Token 示例 | 典型用途 |
| --- | --- |
| `$desktop` | 应用页大底 |
| `$background` | 应用默认背景 |
| `$surface` | 卡片、白底按键 |
| `$button` | 系统默认按钮底（浅色勿与 `$desktop` 糊在一起） |
| `$text` / `$secondary` | 主/次文字 |
| `$accent` / `$pressed` | 强调色、按下态 |
| `$border` / `$error` / `$icon` | 描边、错误、图标色 |

`Ouse.theme.get().colors` 含 `desktop` 与 `textSecondary`（对应 JSON 的 `secondary`）。应用用 token，系统换肤；不要为「好看一点」在应用里复制一整套硬编码灰阶，除非该色不随主题变。

### 1.6 和 UI 相关的其它系统能力

- **软键盘**：`textInput` 共用原生 IME；`keyboardMode` 为 resize / pan / overlay
- **通知**：清单 `notifications: true` 后 `Ouse.notifications.*`（不等于后台执行权）
- **包内资源**：`Ouse.assets.list/readText/readBytes`，`setImage`
- **私有存储**：`Ouse.storage.*`（Promise），按应用 ID 隔离
- **电脑侧**：插件可预览、打包、串口安装/卸载、设备画面镜像（点按/手势）

---

## 2. 后台运行机制

### 2.1 三个容易混的词

| 概念 | 含义 |
| --- | --- |
| **可见（visible）** | 是否在前台占屏；由原生应用管理器决定 |
| **可跑（runnable）** | JS 调度器是否允许执行回调/计时器 |
| **申请了后台（backgroundActive）** | 应用调用了 `Ouse.background.begin()` 且尚未 `end()` |

可见 ≠ 可跑。回桌面后默认：**实例还在、变量还在，但 JS 冻结**。

### 2.2 状态机（对应用可见的）

`Ouse.lifecycle.get().state`：

| state | 大致条件 |
| --- | --- |
| `foreground` | 可见 |
| `background` | 不可见，但已 `begin()`，仍可跑 |
| `suspended` | 不可见，未申请后台 → JS 暂停 |

另有：`visible`、`runnable`、`backgroundAllowed`（清单是否声明）、`backgroundActive`。

### 2.3 怎样才能在后台继续跑

必须两步都满足：

1. 清单：`"capabilities": { "background": true }`
2. 运行时：在合适时机调用 `Ouse.background.begin()`；做完后 `Ouse.background.end()`

只声明能力、不 `begin()` → 离开前台仍挂起。  
通知 `ongoing`、发通知 → **不会**自动打开后台执行权。

未声明后台时调用 `begin()` 会失败（返回 false）。

### 2.4 生命周期回调

可挂在 `globalThis.app`：

```javascript
globalThis.app = {
  onPause()  { /* 即将离开前台 */ },
  onResume() { /* 从后台/挂起回到前台；首次创建不会走这里 */ },
  onClose()  { /* 实例被关掉前 */ },
};
Ouse.lifecycle.onChange((life) => { /* state 变化 */ });
```

- **Home / 回桌面**：默认 `onPause`，未 `begin` 则挂起计时器（恢复时不补跑错过的周期风暴）
- **最近任务划掉 / 卸载 / 显式关闭**：结束 JS，释放控件与计时器；通知记录可另论
- **同 ID 更新包**：一般保留私有存储；界面与脚本按新包装载

### 2.5 执行边界（为什么不能当完整浏览器）

| 项 | 当前行为 |
| --- | --- |
| 运行时 | 各应用独立 QuickJS 堆；共享一个系统脚本任务调度 |
| 前台时间片 | 约 12 ms |
| 后台时间片 | 约 4 ms（更严） |
| 计时器 | 每应用最多 8 个，间隔 100～60000 ms |
| Promise | 每片最多推进有限个微任务，其余延后 |
| 超时/死循环 | 只干掉该应用 JS，不应拖死整机 |
| 存储 I/O | 独立原生任务；挂起时已接受的写仍可完成，JS 回调等恢复后再投递 |

因此：后台适合短轮询、收尾、配合通知；**不要**指望用 JS 做重启后闹钟或长时间占核媒体引擎（那些走后续原生能力）。

### 2.6 一张顺序图

```text
打开应用
  → 建页 + 执行 app.js 顶层
  → state = foreground，计时器可跑

用户按 Home
  → onPause
  → 若未 begin：state = suspended，JS/计时器冻结
  → 若已 begin：state = background，小时间片继续跑

再次点开图标
  → onResume
  → state = foreground

最近任务关闭卡片
  → onClose，销毁上下文（后台申请一并结束）
```

---

## 3. 和预览、真机对比时怎么想

1. **系统栏**：真机有状态栏/手势条；预览通常只画 320×416 内容区。  
2. **主题**：预览色表必须与 `ouse_theme.c` 一致；只改预览或只改应用，对已刷旧固件的设备不够。  
3. **亮度 / JPEG 镜像**：设备画面经截图传输，观感会比本地预览略糊，不代表布局算错。

---

## 4. 相关文档

| 文档 | 内容 |
| --- | --- |
| [bpk.md](bpk.md) | 清单、控件、样式、导航、通知、资源、存储完整契约 |
| [0.5.0-framework.md](0.5.0-framework.md) | 脚本/UI 分离、后台与键盘第一阶段 |
| [0.6.0-framework.md](0.6.0-framework.md) | 组合控件与通知 |
| [0.7.0-framework.md](0.7.0-framework.md) | 包内资源与私有存储 |
| [apps/README.md](../apps/README.md) | 应用侧速查与示例命令 |
