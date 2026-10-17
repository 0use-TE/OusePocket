# OusePocket 应用清单

排除：**应用商店**、**应用管理**（按需求不做）。  
当前固件约 **0.18.2**；用户应用为 `apps/` 下 BPK，桌面容量约再装 23 个 `user.*`。

写新应用请同时看 `.cursor/skills/ouse-pocket-app/SKILL.md`。

---

## 已完成（可用）

### 系统自带

| 名称 | 说明 |
| --- | --- |
| 设置 | 原生；Wi-Fi、显示、声音、电源、导航、USB 等 |

### 日常应用（`apps/`）

| 目录 | ID | 状态 |
| --- | --- | --- |
| music | `user.music` | 曲库滚动/筛选；SD / 内置曲目；无多余「SD CARD」标签 |
| alarm | `user.alarm` | 闹钟 |
| countdown | `user.countdown` | 倒计时 + 通知 |
| stopwatch | `user.stopwatch` | 秒表 |
| explorer | `user.explorer` | SD 资源管理器（浏览/详情/滚动续载/返回栈） |
| notepad | `user.notepad` | 记事本；等存储加载后再画空态 |
| dice | `user.dice` | 骰子 1～6 颗；掷出动画约 2 秒 |
| todo | `user.todo` | 待办；时间用月日时分加减选择，默认当前 |
| guess | `user.guess` | 猜数字 1–100 |
| g2048 | `user.g2048` | 2048；棋盘滑动；最佳分持久化 |
| weather | `user.weather` | IP 粗定位 + 城市搜索（含拼音）；HTTP Open-Meteo；市级 |
| calculator | `user.calculator` | 简单 / 科学 / 程序；图标切模式；模式持久化 |

### 开发 / 诊断（可装，非日常产品）

| 目录 | ID | 说明 |
| --- | --- | --- |
| resource-monitor | `user.resources` | 资源与存储监视 |
| api-test | `user.api-test` | 分类接口测试 |
| service-test | `user.services` | 系统服务测试 |
| framework-test | `user.framework-test` | 框架验证 |
| framework-stage2 | `user.stage2-test` | 框架二阶段验证 |

### 示例目录

| 路径 | 说明 |
| --- | --- |
| `examples/counter` | 计数器示例；`package-firmware` 仍引用，整夹勿删 |
| ~~`examples/calculator`~~ | 已删；正式版在 `apps/calculator` |

### 做过又撤掉

| 应用 | 原因 |
| --- | --- |
| 录音机 `user.recorder` | 固件 `Ouse.audio.record` 仅 **1～3 秒 RAM**，无法安卓式长录/列表落盘；应用与包已删除 |

---

## 可以做（现有 JS/原生能力内）

不改固件、或只加 BPK 就能做的方向（按实用度大致排序）：

| 候选 | 依据 | 备注 |
| --- | --- | --- |
| 单位换算 / 汇率小工具 | UI + storage；汇率需 `network`（优先 HTTP） | 简洁一屏 |
| 番茄钟 / 专注计时 | `timers` + `notifications` + `background` | 可参考 countdown |
| 世界时钟 / 多时区 | `Ouse.clock` | 无复杂日历库 |
| 密码 / 随机串生成 | 纯 JS | 无系统剪贴板则只展示 |
| 进制/色值小工具 | 计算器旁路或独立 | 程序模式已有一部 |
| 节拍器 | `Ouse.audio.testTone` 或短周期 UI | 精度受调度限制 |
| HTTP 状态/接口探针 | `Ouse.net.request` | 真机 HTTPS 常超时，默认 HTTP |
| SD 音乐文件夹快捷入口 | `media` + `sdcard` | 音乐应用已覆盖大半 |
| 亮度/音量快捷页 | `display` / `audio` | 设置与快捷面板已有，可做极简遥控风 |

---

## 暂缓 / 需要固件或硬件后再做

| 候选 | 阻塞 |
| --- | --- |
| 长录音机、语音备忘 | 录音时长与落盘 API |
| 相机 / 相册 / 扫码 | 无摄像头与图库 API |
| 浏览器 | 无 WebView |
| 蓝牙外设 / 耳机应用 | 蓝牙栈与 JS API 未交付 |
| 地图导航 | 无地图组件；网络与性能也不够 |
| 应用商店 / 应用管理 | 明确不做；OPK1 仅有 LIST/OPEN/REMOVE 等，无详情管理 UI API |
| 深睡闹钟、关机唤醒 | 闹钟能力边界见服务文档 |
| 流式解码（MP3 等） | 媒体侧仍以 PCM WAV 等现有路径为主 |

---

## 容量与习惯

- 注册上限约 **24**（含设置 1）；同时前台仅 1 个应用。
- 新应用放 `apps/<name>/`，不要往 `examples/` 堆产品。
- 装包：`python tools/bpk.py pack apps/<name> dist/bpk/<name>.bpk`，再 `--port COMx install`（常用 921600）。
- 口味：简洁、少小字、能一屏就一屏；详见技能 `ouse-pocket-app`。
