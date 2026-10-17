# BPK：清单、控件与系统 API

0.10.0 新增原生 HTTP/HTTPS、WAV 媒体会话、系统时间与持久闹钟，接口和限制见 [原生服务](0.10.0-services.md)。新增服务不授权后台 JS；GPIO/USB 外设接口未开放。验证状态以该文档的交付记录为准。

当前 0.9.0 新增任务 ID/终态事件、后台原因与应用/系统诊断；异步音频和 Wi-Fi 接受请求返回任务 ID（拒绝 false），存储 Promise 增加 taskId。执行预算口径已改为任务获调度时间，以下历史说明中的 12/4 ms 不应解释为纯 JS CPU 时间。接口、取消/关闭策略和计时限制统一见 [0.9.0 系统机制](0.9.0-kernel.md)。

系统服务与桌面仍为原生 C/C++。资源监视和接口测试是可单独安装、删除的 JSON＋JS BPK；固件只保留设置应用。计数器和计算器仅作为可安装的开发示例。

若要先建立整体图景（UI 怎么开发、后台怎么跑、浅色主题和预览的关系），先读 [UI 开发与后台运行机制](ui-and-background.md)。

## 应用清单

```json
{
  "runtime": "ouse-core",
  "api": 1,
  "id": "user.resources",
  "name": "Resources",
  "names": {"zh_CN": "资源监视", "en": "Resources"},
  "icon": "icon.png",
  "capabilities": {"background": false, "notifications": true},
  "assets": [
    {"id": "cover", "type": "image", "path": "assets/images/cover.png"},
    {"id": "words", "type": "text", "path": "assets/i18n/zh_CN.json"},
    {"id": "sample", "type": "binary", "path": "assets/sample.dat"}
  ]
}
```

清单没有包格式版本字段，`format` 会被拒绝。`api: 1` 表示脚本运行时的 API 契约，与包的版号无关。

`id` 使用 `user.` 前缀，仅允许小写 ASCII 字母、数字、点、下划线、连字符，最多 47 字节。`name` 必填，作为默认名称。`names` 可省略，`zh_CN`、`en` 也可分别省略；未提供当前语言名称时回退到 `name`。每个名称为 1～47 个 UTF-8 字节，禁止 NUL。目前系统只支持这两种语言，其他语言键会被拒绝。

桌面和最近应用统一读取清单名称；语言切换后立即更新，不执行应用 JS。名称排版、尺寸和字体由系统控制。名称中使用的字形必须由系统字体覆盖。

源目录的 `icon.png` 推荐为 48×48 RGBA PNG，宽、高各为 1～96 像素。打包工具离线转换为 BGRA8888 像素，包内名称固定为 `icon.bin`，清单中的 `icon` 同时改为 `icon.bin`。板上直接使用 LVGL 图片描述符，不包含 PNG 解码器。也可以直接提供下述格式的 `icon.bin`，这时打包不需要 Pillow。

仓库自带应用和示例显式使用 `icon.bin`，同时保留 `icon.png` 设计源，因此现有 IDF Python 和扩展不需要安装 Pillow 就能打包。修改设计源后，明确执行 `python tools/bpk.py convert-icon apps/resource-monitor/icon.png apps/resource-monitor/icon.bin`（转换这一步需要 Pillow），或把清单改为 `icon.png` 让打包时重新转换。工具不会在缺少 Pillow 时偷偷回退到可能已过期的同名二进制图标。

## 应用内部文案

应用框架的执行和输入基础见 [0.5.0 框架说明](0.5.0-framework.md)，当前契约见本文。可选 `capabilities` 只接受布尔值 `background`、`notifications`、`network`、`media`、`alarms`，省略时均为 false，不引入另一种 BPK 格式。声明后台能力后，仍需主动调用 `Ouse.background.begin()` 才会在离开前台后继续执行；工作结束调用 `end()`，未申请时默认暂停。通知能力单独控制消息发布，不自动授权后台运行。

首次创建执行 `app.js` 顶层代码，不调用 `app.onResume()`。顶层完成初始化，已创建实例从后台返回时才执行 onResume；还可注册 onPause/onClose 和 `Ouse.lifecycle.onChange(fn)`。显式关闭结束 JS，原生通知记录可以继续存在，点击后重新创建所属应用。

应用自己维护词条。当前 JSON 页面支持 `translations.zh_CN`、`translations.en` 与文本控件的 `textKey`，列表项另有 `subtitleKey`；动态 JS 使用 `Ouse.locale.t(key)` 与 `Ouse.locale.onChange(callback)`。这些词条属于 BPK，不需要写入固件全局词表。

本版页面格式仍为 `page.json`，脚本为 `app.js`。XML 不是已实现的页面格式；如果以后需要，须另加解析和布局支持。使用现有 JSON＋JS 即可完成标题、正文和动态数据的本地化。

## 页面与组合控件

页面包含 `api: 1`、`units: "dp"`、可选 `translations` 和 `root`。节点 ID 必填且唯一；width、height 必填。dp 模式可用 width: "fill"，height: "fill"/"content"；x/y 默认 0。布局使用 row/column，文字大小以 sp 设置。现有 px 入口要求数值位置和尺寸，建议新应用直接用 dp。

| 用途 | type |
| --- | --- |
| 布局和组合 | container、card、list、listItem、toolbar、divider |
| 文本与图片 | label、image |
| 输入和操作 | button、slider、textInput、switch、checkbox、dropdown |
| 状态展示 | progressBar、spinner、arc |
| 应用弹层 | dialog、bottomSheet |

共 19 种。card 默认纵向、16 dp 内边距、20 dp 圆角；list 默认纵向滚动；listItem 默认横向，原生标题/副标题区域占用剩余空间，children 可放右侧开关等控件。列表项至少 56 dp，含副标题时至少 76 dp；toolbar 默认横向、至少 56 dp。标题通过 text/textKey 设置。它们都是普通 LVGL 组合控件，子按钮继续通过相同事件入口绑定。

```json
{
  "api": 1,
  "units": "dp",
  "translations": {
    "zh_CN": {"title": "我的应用", "option": "自动同步", "detail": "使用无线网络"},
    "en": {"title": "My App", "option": "Auto sync", "detail": "Use Wi-Fi"}
  },
  "root": {
    "type": "container", "id": "page", "width": "fill", "height": "fill",
    "layout": "column", "padding": 12, "gap": 12,
    "children": [
      {"type": "toolbar", "id": "title", "textKey": "title", "width": "fill", "height": 56},
      {"type": "card", "id": "card", "width": "fill", "height": "content", "children": [
        {"type": "listItem", "id": "syncRow", "textKey": "option", "subtitleKey": "detail",
         "width": "fill", "height": "content", "children": [
           {"type": "switch", "id": "sync", "width": 48, "height": 28, "value": true}
        ]}
      ]}
    ]
  }
}
```

`Ouse.ui` 提供 setText/getText、setValue/getValue、setImage、on、setStyle、focus/blur、show/hide/isVisible、setEnabled。设置值不会合成用户 change 事件。`on(id,event,fn)` 的事件为 click、change、focus、blur、submit、dismiss；传 null 解除当前监听。

## 子页面与导航

原来的单 `root` 页面继续使用，导航 ID 固定为 `main`。多页面使用 `pages` 数组和必填 `initialPage`；`root` 与 `pages` 只能提供一个。units 和 translations 仍放在文档顶层，所有页面共用。页面 ID 匹配 `[A-Za-z][A-Za-z0-9_-]{0,30}`，区分大小写且不能重复；initialPage 必须指向其中一页。

```json
{
  "api": 1,
  "units": "dp",
  "initialPage": "home",
  "pages": [
    {"id": "home", "root": {
      "type": "container", "id": "page", "width": "fill", "height": "fill",
      "layout": "column", "padding": 16, "children": [
        {"type": "button", "id": "details", "text": "打开详情", "width": "fill", "height": 44}
      ]
    }},
    {"id": "details", "root": {
      "type": "container", "id": "page", "width": "fill", "height": "fill",
      "layout": "column", "padding": 16, "children": [
        {"type": "button", "id": "back", "text": "返回", "width": "fill", "height": 44}
      ]
    }}
  ]
}
```

最多定义 16 页，导航栈最多 8 层；page.json 总大小仍为 32 KiB。每页独立验证，单页最多 96 节点，跨页可复用节点 ID。安装时验证所有页的布局和资源引用，不执行 app.js。运行时只建立当前页的模型与 LVGL 对象，其他页面保留静态 JSON 定义，不建立隐藏控件。

| 方法 | 返回与行为 |
| --- | --- |
| `push(pageId)` | 压入目标页；已接受为 true |
| `back()` | 弹出当前页；根页返回 false，不自动回桌面 |
| `replace(pageId)` | 替换栈顶，深度不变；已接受为 true |
| `get()` | `{page,depth,canGoBack,pending}`，page 为当前页 ID |
| `onChange(fn或null)` | 注册或解除一个监听，返回 undefined |

push/back/replace 异步切换。忙碌、非前台、尚未初始化、栈边界或 push/replace 已在当前页时返回 false；错误参数和未知页面 ID 抛异常。接受请求后 pending 为 true，get 的 page 保持旧页，直到切换完成；此时应结束当前事件处理，等待 onChange 后再访问目标页节点。多个未完成请求不排队。

onChange 在页面对象建立、布局与显示快照更新后投递 `{page,previous,action,depth,canGoBack}`。action 为 open/push/back/replace；首次 open 的 previous 是空字符串。顶层注册监听后，首次 open 会等待界面实际建立再发送；没有监听时保留最近一次未投递的进入事件。建议在该监听中绑定当前页控件并恢复业务值。

```javascript
Ouse.navigation.onChange(event => {
    if (event.page === "home") {
        Ouse.ui.on("details", "click", () => Ouse.navigation.push("details"));
    } else if (event.page === "details") {
        Ouse.ui.on("back", "click", () => Ouse.navigation.back());
    }
});
```

离开页面会收起输入法、下拉列表和模态，释放控件、输入队列、节点监听与图片缓存。返回时按 JSON 重建，不保留滚动位置、表单值或动态样式；需要保留的业务值由应用放在 JS 变量或私有存储，并在 onChange 恢复。页面切换共用同一个 JS 上下文，不触发应用 onPause/onResume，不取消已有计时器或存储请求。

界面重建期间暂缓本实例的普通 JS、Promise 和存储完成回调，建立后先投递 onChange，再处理延后的工作。计时器和 Promise 仍可能在别的页面继续完成，应用应检查当前页面或自有页面代次，避免旧页面的异步逻辑修改新页面同名控件。旧页面输入事件带有代次，不能投递到新页。系统 Back 先收键盘，再关模态，再弹出页面栈；根页交给系统回桌面。

## 增量样式与主题

节点 `style` 和 `Ouse.ui.setStyle(id, patch)` 使用相同字段：backgroundColor、textColor、borderColor、accentColor、thumbColor、borderWidth、radius、opacity、textAlign、hidden、disabled、flexGrow。setStyle 增量合并，未提供项不会重置；show/hide 和 setEnabled 仅改变对应状态。isVisible 返回当前应用和逻辑父节点共同决定的可见性。

颜色可为 `#RRGGBB` 或系统引用 `$background`、`$desktop`、`$surface`、`$text`、`$secondary`、`$accent`、`$button`、`$pressed`、`$border`、`$disabled`、`$error`、`$icon`。系统引用随明暗主题更新，显式颜色由应用保持。颜色 null 移除本地覆盖并恢复默认。

```javascript
Ouse.ui.setStyle("card", {backgroundColor:"$surface", borderColor:"$border", borderWidth:1});
Ouse.ui.setStyle("card", {radius:20}); // 保留上一次的背景和边框
Ouse.ui.setStyle("syncRow", {
    states: {
        pressed: {backgroundColor:"$pressed"},
        disabled: {textColor:"$disabled", opacity:128}
    }
});
Ouse.ui.setStyle("syncRow", {states:{pressed:null}});
Ouse.ui.setEnabled("syncRow", false);
```

states 可设 pressed、checked、disabled，各自支持 backgroundColor/textColor/borderColor/accentColor/thumbColor/opacity。`style.disabled` 是禁用布尔值，与 `style.states.disabled` 的视觉覆盖不同。禁用容器同时禁止其原生子控件输入。

开关和复选框的状态背景同时映射到实际轨道/标记；checked.backgroundColor 优先于通用 accentColor。accentColor 设置进度、滑块、圆弧的有效部分，或开关/复选框的选中颜色；thumbColor 设置开关、滑块和可调圆弧的拇指。圆弧的 backgroundColor 设置底层轨道颜色，仍保持矩形背景透明。颜色和状态可传 null 恢复默认；未覆盖的开关为圆轨道和白色拇指，复选框提供对勾，控件不依赖 LVGL 默认主题。

## 对话框与底部弹层

弹层与普通控件一起声明，初始隐藏，内部 children 使用现有控件和事件。dialog 居中，bottomSheet 在应用底部；遮罩属于该应用。不能在模态内部再嵌套模态。

```json
{
  "type": "dialog", "id": "confirm", "text": "确认操作",
  "width": 264, "height": "content",
  "children": [
    {"type": "label", "id": "question", "text": "继续吗？", "width": "fill", "height": "content"},
    {"type": "button", "id": "confirmDone", "text": "确认", "width": "fill", "height": 44}
  ]
}
```

```javascript
Ouse.ui.show("confirm");
Ouse.ui.on("confirmDone", "click", () => Ouse.ui.hide("confirm"));
Ouse.ui.on("confirm", "dismiss", reason => console.log(reason));
```

显示一个模态会隐藏其他模态；后台调用 show 不会打开弹层或抢走前台。系统 Back 先收输入法，再关模态，最后处理应用导航。背景点击和 Back 分别派发 dismiss 的 backdrop/back 原因，此时 isVisible 已为 false。显式 hide 不派发 dismiss。应用切到后台时弹层收起，回来后不自动重开。

输入框和系统键盘仍使用 `textInput`、inputType（text/number/password）、keyboardMode（resize/pan/overlay）；显示中文与中文拼音输入是两件事，目前没有拼音候选输入服务。

## 通知 API

应用先在清单声明 `"capabilities":{"notifications":true}`。顶部左侧下拉打开系统通知列表，右侧下拉打开快捷面板；应用消息标题、正文由应用自己本地化，系统从注册表取得应用名称和图标。

```javascript
Ouse.notifications.onOpen(event => {
    console.log(event.id + ":" + event.payload);
    // 根据 payload 显示对应页面；新实例应在顶层安装此监听。
});
const accepted = Ouse.notifications.publish({
    id:"download", title:"下载完成", body:"点击查看", payload:"file:example"
});
Ouse.notifications.update({id:"download", title:"已完成", body:"点击查看", payload:"file:example"});
const mine = Ouse.notifications.list();
Ouse.notifications.cancel("download");
```

publish/update 根据当前应用 ID 与通知 id 创建或完整替换一条记录；update 是 publish 的同义入口，并非字段增量更新。id/title 必填且非空，body/payload 默认为空，ongoing 默认为 false。publish/update/cancel 返回是否成功；缺少能力时返回 false，list 返回空数组。参数格式或长度错误会抛异常。onOpen 接受函数或 null，返回当前应用是否具有通知能力。

list 只返回当前应用的记录，按更新较新优先，字段为 id/title/body/payload/ongoing/createdMs/updatedMs；时间是本次启动后的毫秒。应用不能指定别的 app ID，也不能读取其他应用通知。

普通通知可单条清除或清除全部。ongoing 不被用户清除，应用可主动 cancel 或更新为普通通知；设置 ongoing 不会创建后台任务。记录保留在 RAM，应用普通关闭后仍在，设备重启后消失；替换和卸载会清除对应应用记录。

点击先保存 id/payload 快照，再打开所属应用；已存在的挂起实例恢复，关闭的实例重新执行 app.js。前台安装 onOpen 后才投递，回调成功后才确认。没有监听的待投递事件最多保留 5 分钟；取消/清除对应通知也会撤销待投递事件。通知更新不改写已经点击的 payload 快照。

每应用最多 6 条、全系统最多 24 条；满额后新建返回 false，已有 ID 仍可更新。待点击事件全局最多 8 个、每应用最多 4 个。通知 ID 最多 47、title 127、body 383、payload 255 个 UTF-8 字节。

## 包内资源

`assets` 可省略，或为最多 16 项的数组；每项恰好包含 `id`、`type`、`path`。ID 跨全部资源唯一，匹配 `[A-Za-z][A-Za-z0-9_-]{0,30}`，区分大小写。路径跨资源唯一，最多 95 个 ASCII 字节，必须是 `assets/` 下的规范相对路径；每段匹配 `[A-Za-z0-9._-]+`，不能是 `.` 或 `..`。禁止空段、反斜杠、冒号、绝对路径及目录条目。

| type | 内容 | 每项限制 | 同类累计限制 |
| --- | --- | --- | --- |
| image | OIC1 头＋BGRA8888 像素 | 宽、高各 1～512；文件长度精确为 8＋宽×高×4 | 图片像素总计 131,072 字节，不含 8 字节头和启动器图标 |
| text | 无 NUL 的合法 UTF-8，可自行保存 JSON、词表等 | 16,384 字节，允许空文件 | text＋binary 总计 131,072 字节 |
| binary | 任意字节，包括 0 和 255 | 65,536 字节，允许空文件 | 与 text 共用上述总量 |

源清单 image 的路径可指向 PNG。`pack` 使用 Pillow 转换成同目录、同名 `.bin`，并改写包内清单；例如 `assets/images/cover.png` 变为 `assets/images/cover.bin`。转换后路径仍须唯一，与其他资源冲突会报错。源文件不被修改。预转换 OIC1 资源直接打包，不需要 Pillow。板上没有 PNG 解码器，图片不预乘 alpha。

```json
{"type":"image", "id":"coverView", "src":"cover", "width":96, "height":96}
```

```javascript
const resources = Ouse.assets.list();
// image: {id,type,bytes,width,height}；text/binary: {id,type,bytes}
const words = JSON.parse(Ouse.assets.readText("words"));
const sample = new Uint8Array(Ouse.assets.readBytes("sample"));
Ouse.ui.setImage("coverView", "cover");
```

这些资源入口同步读取当前实例已经持有的内存，不在 JS 或 UI 线程读文件。image 的 bytes 是像素字节数；其他类型是文件长度。readText 只接受 text；readBytes 接受 text/binary，返回独立的 ArrayBuffer 副本。不能用这两个方法读取 image。错误 ID 或类型抛异常。setImage 接受现有 image 控件 ID 和资源 image ID，正常返回 undefined；由后续 UI 调度更新 LVGL 图片和缓存。

包内资源只读。应用实例持有独立资源副本；关闭、更新和卸载按 UI 图片缓存、JS 实例及资源的所有权顺序清理，应用不能借资源路径访问其他包或私有目录。

## 应用私有存储

`Ouse.storage` 的六个方法全部返回 Promise。系统根据当前应用 ID 选择独立私有空间，应用不能提供别的 app ID 或文件路径。数据不放在只读 BPK 内；同 ID 的包更新、普通关闭和重启保留数据，卸载会删除该应用的私有数据。

| 方法 | resolve 值 |
| --- | --- |
| `get(key)` | 保存的 JSON 值；不存在为 null |
| `set(key,value)` | 提交成功为 true |
| `remove(key)` | 已删除为 true，原本不存在为 false |
| `keys()` | 按 ASCII 字节顺序排序的 key 数组 |
| `clear()` | 本次删除的 key 数量 |
| `info()` | `{usedBytes,quotaBytes,keyCount,keyLimit,valueLimit}` |

```javascript
Ouse.ui.on("save", "click", () => {
    Ouse.storage.set("preferences", {volume: 60, title: "你好"})
        .then(() => Ouse.storage.get("preferences"))
        .then(value => Ouse.ui.setText("value", String(value.volume)))
        .catch(error => console.log(String(error.code || error.message)));
});
```

key 为 1～31 个 ASCII 字符，首字符为字母或数字，其余仅字母、数字、`.`、`_`、`-`。每应用最多 32 个 key，序列化后的单值最多 8,192 个 UTF-8 字节，全部已提交 JSON 值合计最多 65,536 字节。info 的 usedBytes 使用这份逻辑字节数；quotaBytes/keyLimit/valueLimit 分别为 65536/32/8192。覆盖已有 key 时，先扣除旧值再计算配额；超限拒绝不会替换旧值。

set 在当前 JS 调用内按标准 `JSON.stringify` 生成快照，再交给原生存储任务；随后修改原对象不影响已提交请求。顶层 undefined/函数不能序列化为 JSON，会 reject；循环引用、BigInt 或抛异常的 toJSON/getter 也会 reject。NaN/Infinity 变成 null，数组中的 undefined 变成 null，对象值为 undefined 的属性会被省略；Date/toJSON 按标准 stringify 处理。这里没有自定义二进制序列化，ArrayBuffer 如需持久化，应由应用转为 JSON 支持的形式。

参数错误、配额、队列满及文件错误通过 reject 返回；本地参数错误可能带 INVALID_ARGUMENT、INVALID_VALUE、VALUE_TOO_LARGE、BUSY，原生错误带对应 ESP 错误 code。应用应及时安装 catch；未处理拒绝仍会进入应用错误页。每实例最多 8 个在途存储请求，原生请求队列也有上限，建议串行 await 或限制并发。

文件 I/O 全部由独立原生任务处理。set 的 Promise 成功表示该值已写入、同步并完成临时文件替换；它不是多个 key 的事务。实例挂起时，已接受的 I/O 继续，Promise 的 JS 回调等恢复前台或明确启用后台执行后才投递。关闭会释放该实例的 resolver，原生任务仍安全完成已接受的写入；卸载先关闭应用，再按队列顺序排空其旧写入并清除数据，旧回调不会把数据重新写回来。设备文件系统损坏时不会自动格式化已有内容。

## 存储用量快照

`Ouse.system.storage()` 同步返回原生任务维护的缓存，不返回 Promise，也不在查询时扫描 Flash：

```javascript
const disk = Ouse.system.storage();
// {ready, revision, totalBytes, usedBytes, freeBytes,
//  packageBytes, privateBytes, packageCount, privateApps}
```

- totalBytes/usedBytes/freeBytes：LittleFS 数据分区报告的总量、实际占用与剩余，包含文件系统块和元数据开销；不是整颗 Flash 容量。物理 Flash 容量仍读 `Ouse.system.info().flashBytes`。
- packageBytes：已安装 `.bpk` 文件的逻辑长度总和；packageCount 为包文件数量。
- privateBytes：已提交私有 JSON 值的逻辑长度总和；privateApps 为至少有一个已提交 key 的应用数量。
- ready 表示存储已初始化；revision 随后台缓存刷新递增。原生存储操作和包变更后刷新，连续查询可能读到同一版本。

文件系统的实际占用与逻辑文件长度不是同一种统计，不能用 packageBytes＋privateBytes 代替 usedBytes。单应用私有配额和 key 数量读异步 `Ouse.storage.info()`。

## 文件和限制

BPK ZIP 必须恰好包含 `manifest.json`、`page.json`、`app.js`、`icon.bin` 四个非空固定文件，加上清单声明的全部 assets，不能遗漏或额外添加文件。所有文件使用 ZIP_STORED；禁止目录条目、重复文件、压缩、加密、额外字段、注释、数据描述符、ZIP64、分卷、前置或尾随数据。只有已声明资源允许上节规定的 assets/ 路径。中央目录和本地头的名称、偏移、大小、CRC 等必须一致。

固件固定 GCC 15 RISC-V 与 QuickJS 0.14.0，构建时只给 QuickJS 组件补 `-fno-strict-aliasing`，保留 `-O2`。原因是该工具链的 uint32_t 为 unsigned long，而引擎字符串迭代器通过 int 指针更新该类型的索引；修复前真机已复现 for-of/iterator.next 重复首字符。build4 已完成同一样例的前后对照，两种迭代均正确读取 `320中文😃` 的字符并正常结束，显式 codePoint 索引也正确。正常 12/4 ms 回调预算不变，证据与固件身份见运行时 PORTING.md。

- 整包上限为 524,288 字节，USB 串口协议是 OPK1，与 BPK 格式无关。
- 清单上限为 8,192 字节；页面和脚本各最多 32,768 字节。
- 每页最多 96 个 JSON 节点，根深度 0、最多到深度 8；节点 ID 最多 31 个 UTF-8 字节且在该页唯一，文本与词条值最多 256 字节，中英文词表各最多 128 条。最多 16 页，导航栈最多 8 层。
- 每应用 JS 堆限制 512 KiB，共享工作任务栈 64 KiB，QuickJS 栈限制 40 KiB；常规前台每轮 12 ms、后台 4 ms。上下文与 JSON 文档初始化、每页 schema 校验、初始页面模型、隔离编译验证、首次脚本求值分别使用 100 ms；切页的单页模型解析也独立计 100 ms，页间校验让出一个调度 tick。该值不代表整个打开或验证流程的总耗时。预算不能抢占任意原生 C 调用；编译验证调用者最多等待 2 秒，与单阶段预算分开计算。
- 每应用 8 个计时器，延迟/周期 100～60,000 ms；输入队列 16 个事件，每轮最多处理 4 个；每个调度片段最多处理 8 个 Promise job，剩余微任务延后至下一可运行片段，不因第 9 个 job 直接判定应用失败。
- 输入框 maxLength 为 1～256、默认 128；下拉框最多 24 项、全部选项字符串最多 512 字节。
- `icon.bin`：4 字节 ASCII `OIC1`，接 2 字节小端宽、2 字节小端高，再接 `width * height * 4` 字节 BGRA8888。每个尺寸 1～96，像素数据最多 36,864 字节，不预乘 alpha。
- 固件会验证 UTF-8、名称长度、图像尺寸及精确字节数、ZIP CRC 与目录一致性。
- BPK 最多 20 个文件（4 固定＋16 资源）；不提供 XML 页面解析、模块导入、任意文件或通用网络访问。

## 打包与安装

在仓库根目录执行：

```powershell
python tools/bpk.py pack apps/resource-monitor dist/bpk/resource-monitor.bpk
python tools/bpk.py pack apps/api-test dist/bpk/api-test.bpk
python tools/bpk.py pack examples/counter dist/bpk/counter.bpk
python tools/bpk.py pack examples/calculator dist/bpk/calculator.bpk
python tools/bpk.py --port COM10 --baud 921600 install dist/bpk/resource-monitor.bpk
python tools/bpk.py --port COM10 --baud 921600 install dist/bpk/api-test.bpk
```

含 PNG 的打包需要本地 Pillow；上传需要 pyserial 3.5。CLI 与当前固件默认串口波特率为 921600，必须一致；连接明确使用 115200 的固件时传 `--baud 115200`。上传前先关闭占用同一串口的监视器。此处是打包和安装用法；是否已烧录、安装及通过真机检查，以对应固件版本记录为准。

VS Code / Cursor 扩展识别相同清单、资源路径/类型、长度、图像和累计配额，可直接编辑默认、中英文名称。图标与全部图片资源均已预转换为 OIC1 时，打包只用标准库；任何图标或资源选择 PNG 而当前 Python 缺少 Pillow 时，都会明确报错，不会静默换图。


## 0.11.0 健康与后台活动

新增 `Ouse.system.health()`、`activities()`、`stopActivity(taskId)` 和 info 内存最大连续块/最低余量缓存；重启只在原生设置提供。活动列表有容量限制，停止请求以公共任务终态为准。使用方式、口径与异常恢复见 [0.11.0 可靠性](0.11.0-reliability.md)。


## 0.12.0 默认文字大小

系统不再提供用户字号倍率，`Ouse.display.setFontPercent` 已移除；`Ouse.display.get()` 的 fontPercent/fontScale 固定 100/1。JSON `fontSize` 保留 8..84 整数请求范围，选择最接近的实际 8..40 角色字体；超过 40 的请求使用 40px。此处是固定字体池的就近选择，不承诺请求像素数必然有对应字库。标题、正文、数字可以使用不同字号，详见 [默认字号与字体池](0.12.0-default-fonts.md)。
