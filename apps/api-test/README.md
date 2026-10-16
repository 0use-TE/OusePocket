# 接口测试 BPK

`user.api-test` 是独立安装应用，不编进固件。首页左上角显示应用名，下面按功能分类；子页顶部返回和结果区域固定，内容区独立滚动。每页重新建立控件，业务草稿保存在同一个 JS 实例中；仅刷新当前页。

```powershell
python tools/bpk.py pack apps/api-test .tools/user.api-test.bpk
python tools/bpk.py --port COM10 --baud 921600 install .tools/user.api-test.bpk
```

## 页面和操作入口

| page ID | 中文入口 | 主要操作 |
| --- | --- | --- |
| home | 接口测试 | 显示与外观、音频与麦克风、Wi-Fi、电源与系统、控件与输入、通知与后台、资源与存储 |
| display | 显示与外观 | 切换明暗、切换语言、切换字号、亮度滑块、刷新 |
| audio | 音频与麦克风 | 音量、切换静音、测试音、录音 3 秒、回放、停止、电平 |
| wifi | Wi-Fi | Wi-Fi 开关、搜索网络、SSID/密码、连接/断开/忘记、扫描结果选择 |
| power | 电源与系统 | RAM/Flash/CPU/电源快照、自动熄屏、立即熄屏、唤醒屏幕 |
| controls | 控件与输入 | 基础控件、样式与弹层、输入 · 调整布局/平移/覆盖 |
| widgets | 基础控件 | 卡片/工具栏/按钮、开关、复选框、下拉框、滑块、进度、圆弧、加载指示 |
| styles | 样式与弹层 | 增量样式、恢复样式、启用/禁用、对话框、底部弹层 |
| inputResize | 输入 · 调整布局 | 文本、数字、密码，聚焦文本、收起键盘 |
| inputPan | 输入 · 平移 | 同上，使用 pan |
| inputOverlay | 输入 · 覆盖 | 同上，使用 overlay |
| messages | 通知与后台 | 发送/更新/常驻/取消通知，开启/停止后台，回桌面观察，结束全部测试 |
| resources | 资源与存储 | 列出资源、切换图片、读取文本/二进制、错误参数测试、私有数据 |
| storage | 私有数据 | 读取、写入、删除、列出键、清空、用量、错误参数测试 |

所有页共用 `topTitle`、`body`、`result` ID；首页不放“桌面”按钮，子页保留 `back` 返回上一级，回桌面使用系统手势。通知与后台页仍保留 `backgroundHome` 测试入口。只在 navigation.onChange 目标页已建立后绑定本页事件，异步存储回调检查页面与进入次数，避免更新离开的页面。明暗主题使用系统 token，静态文案使用 JSON 中英词表。

设备异步操作先显示“处理中”，等 tasks.get/onComplete 中该任务的终态与 error 确认后显示“操作完成”或错误；被后续请求替代、等待超时也分别显示。明暗/语言/字号/亮度/音量等配置入口只确认快照中的当前值已更新，不将受理返回值当作 Flash 持久化完成。离开音频页或挂后台会停止本应用发起的录放。

文本按转换前的 String 值缓存，仅在内容变化时做 UTF-8 截断和 setText；页面进入次数变化时清空缓存，输入框收到 change 时失效对应值。Wi-Fi 的 AP 行文本和显隐均按值更新，进入页面时重置；网络状态由入页和 Wi-Fi onChange 刷新。500 ms 定时器仅刷新音频、电源、通知与后台这些动态页，同时继续检查所有待完成请求，在当前页及时显示完成、失败或超时结果。固件回调预算保持前台 12 ms、后台 4 ms。

Wi-Fi 入页回调只绑定基础操作并显示摘要；AP 行绑定与绘制由一个 100 ms setTimeout 链分批完成，每次最多两行，初始列表保持隐藏。列表实际变化时替换后续批次，页面进入次数和数据代次共同阻止旧任务写入；离页、暂停、关闭均取消未完成计时器。点击使用该行已显示的网络信息，分批更新期间不会因新扫描列表索引变化而选错网络。扫描完成通过统一任务 ID、终态及 error 反馈，不以列表绘制完成代替设备操作完成。

## 自动操作可识别信息

- 首页分类按钮 ID 为 `go_display`、`go_audio`、`go_wifi`、`go_power`、`go_controls`、`go_messages`、`go_resources`，中文文本见上表。
- 控件分类下为 `go_widgets`、`go_styles`、`go_inputResize`、`go_inputPan`、`go_inputOverlay`。输入页的三个输入框 ID 为 `text`、`number`、`password`，`inputEvents` 内容为 `输入框ID · focus/blur/submit` 或 `输入框ID · change · 长度`。输入变更也会更新固定底部 result，例如 `Input changed · text / 1`，便于键盘打开时观察；只显示字符数，不显示输入内容或密码。
- 控件状态在 `state`；`target` 是“测试按钮”，点击后状态含“点击 N”；另有 `switch`、`checkbox`、`dropdown`、`slider`、`arc`、`progress`。样式页为 `stylePatch`、`styleReset`、`toggleEnabled`、`openDialog`、`openSheet`、`dialogClose`、`sheetClose`。
- 资源页的 `picture` 初始为 blue；“切换图片”（swap）交替 blue/green，结果为“图片已切换 · green/blue”。“读取文本”（readText）返回当前语言的包内词条；“读取二进制”（readBytes）结果含 `0, 17, 128, 255`。资源错误参数测试预期“非法资源访问已拒绝 · 2/2”。
- 私有数据输入框 `key` 默认 `demo`，`value` 默认为 `{"message":"hello","count":1}`。按钮 ID 为 get/set/remove/keys/clear/info/invalid，中文为“读取/写入/删除/列出键/清空/用量/错误参数测试”。Promise 成功后 result 为“操作成功”，JSON 结果在 `output`，始终使用 JSON.stringify 展示（字符串保留引号和控制字符转义），用量在 `state`。错误参数测试输出 `{"rejected":2}`，不写入有效数据。
- 通知按钮 publish/update 使用 `api-demo`，ongoing 使用 `api-ongoing`；cancel 取消这两条测试通知。通知点击会进入 messages，状态显示“通知点击 N”及通知 ID/payload。
- backgroundOn/backgroundOff 对应“开启后台/停止后台”；backgroundHome 为“回桌面观察”。默认后台冻结，只有明确点开启后台才继续计数，每 500 ms 增加一个“后台计数”。cleanup“结束全部测试”会停止后台、取消两条测试通知并停止本应用音频。

普通关闭保留测试通知，方便从通知重新打开应用；通知只在 RAM 中。关闭会结束 JS/后台和本应用音频。私有 JSON 在普通关闭和同 ID 更新后保留，卸载时删除。请在测试结束时点“结束全部测试”，无需卸载这个应用。

包内附两张 32×32 OIC1 图片、中英 JSON 文本和含 0/255 的二进制样本。页面与脚本各仍受 32 KiB 限制；page.json 使用紧凑排版。当前 HTML 预览不等同于完整多页面固件运行环境，实际行为应在设备上验证；本目录文档不代表真机测试已通过。

0.9.0 的通知后台分类增加任务探针、取消请求和系统诊断；background.begin 附带 api-test 原因，存储探针读取自己的 info，不改用户数据。原生硬件结果按任务 ID 判定，页面切换仍用页面代次控制绘制。
