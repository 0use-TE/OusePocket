# OusePocket 应用清单

**应用商店**支持自定义 HTTP/HTTPS 地址；**应用管理**已在设置中提供。资源管理器点击 SD 卡中的 `.bpk` 可安装到内部 Flash。
当前固件 **0.32.0**；用户应用为 `apps/` 下 BPK，注册表按需扩容，安装数量受 Flash 与可分配内存约束。商店与 SD 安装共用原生校验和系统确认，包内包含作者、介绍、发布日期；网页提供真实图标、分类和独立发布页。服务端见 `cloud/store`。网易云使用真实 HID 就绪状态区分配对、订阅与占用，不显示主机播放状态；鼠标应用使用原始触摸事件跟手移动和按住拖拽。录音机列表仅在内容或交互状态变化时重新渲染。天气刷新使用已有城市，显式定位失败可回退已有位置。

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
| camera | `user.camera` | 拍照保存到 SD，相册快捷入口；不提供录像入口 |
| gallery | `user.gallery` | 连续滚动缩略图、照片/系统 AVI 预览、日期、确认删除 |
| recorder | `user.recorder` | SD WAV 录音、结束命名、时长/结束日期、列表播放及删除 |
| alarm | `user.alarm` | 闹钟 |
| countdown | `user.countdown` | 倒计时，到时重复嘀声直到确认 + 通知；后台可继续，关闭应用取消 |
| stopwatch | `user.stopwatch` | 秒表 |
| explorer | `user.explorer` | SD 资源管理器（浏览/详情/滚动续载/返回栈） |
| notepad | `user.notepad` | 记事本；等存储加载后再画空态 |
| dice | `user.dice` | 骰子 1～6 颗；掷出动画约 2 秒 |
| todo | `user.todo` | 待办；时间用月日时分加减选择，默认当前 |
| guess | `user.guess` | 猜数字 1–100 |
| g2048 | `user.g2048` | 2048；棋盘滑动；最佳分持久化 |
| minesweeper | `user.minesweeper` | 扫雷 8×8 / 10 雷；开/旗模式；踩雷与通关音效 |
| tictactoe | `user.tictactoe` | 井字棋；人机；胜负平统计 |
| reaction | `user.reaction` | 反应力；等变色再点；最佳毫秒 |
| rps | `user.rps` | 猜拳（石头剪刀布） |
| mole | `user.mole` | 打地鼠；三档难度；锤子命中；最高分持久化 |
| klotski | `user.klotski` | 华容道（横刀立马）；点块再点空位；最少步数 |
| point24 | `user.point24` | 24点；四张牌合成；撤销/重置/下一题 |
| weather | `user.weather` | IP 粗定位 + 城市搜索（含拼音）；HTTP Open-Meteo；市级 |
| calculator | `user.calculator` | 简单 / 科学 / 程序；图标切模式；模式持久化 |

### 开发 / 诊断（可装，非日常产品）

| 目录 | ID | 说明 |
| --- | --- | --- |
| resource-monitor | `user.resources` | 资源与存储监视 |
| network | `user.network` | Ping / HTTP(S) / WebSocket / MQTT；右上角图标切换模式 |
| api-test | `user.api-test` | 分类接口测试 |
| service-test | `user.services` | 系统服务测试 |
| framework-test | `user.framework-test` | 框架验证 |
| framework-stage2 | `user.stage2-test` | 框架二阶段验证 |

### 示例目录

| 路径 | 说明 |
| --- | --- |
| `apps/counter` | 计数器示例 |

### 做过又撤掉

| 应用 | 原因 |
| --- | --- |
| 早期录音机 | 旧版仅 1～3 秒 RAM；现已由 Core 0.22.0 的 SD 录音服务及 `user.recorder` 替代 |

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
| 扫码 | 已有摄像头 API，尚无二维码识别服务；相机和相册见 0.20.0 文档 |
| 浏览器 | 无 WebView |
| 蓝牙外设 / 耳机应用 | 蓝牙栈与 JS API 未交付 |
| 地图导航 | 无地图组件；网络与性能也不够 |
| 应用商店 | 尚未交付；原生应用管理已在设置提供 |
| 深睡闹钟、关机唤醒 | 闹钟能力边界见服务文档 |
| 流式解码（MP3 等） | 媒体侧仍以 PCM WAV 等现有路径为主 |

---

## 容量与习惯

- 不再固定注册数量；同时前台仅 1 个应用，运行实例与任务仍有资源保护上限。已安装包仅保留目录信息和图标，启动时由脚本工作线程读取、校验并加载资源。
- 新应用放 `apps/<name>/`，不要往 `examples/` 堆产品。
- 装包：`python tools/bpk.py pack apps/<name> dist/bpk/<name>.bpk`，再 `--port COMx install`（常用 921600）。
- 口味：简洁、少小字、能一屏就一屏；详见技能 `ouse-pocket-app`。
