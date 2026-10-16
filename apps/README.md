# JSON＋JS 应用接口

桌面、设置、手势与系统服务为原生 LVGL/C，固件默认只注册设置。`resource-monitor`、`api-test` 是可安装的 BPK；计数器、计算器见 `examples/counter`、`examples/calculator`。`framework-test`、`framework-stage2` 是框架验证应用，不会默认安装到设备。

应用目录包含 `manifest.json`、`page.json`、`app.js` 和图标，还可声明图片、文本、二进制资源。当前核心 C API 为 8，脚本运行时 API 为 1；单一 BPK 契约、完整参数和返回值见 [BPK 说明](../docs/bpk.md)。修改系统中文后先运行 `tools/generate-core-font.py` 再构建；应用文案由应用自己维护，显示的字形仍需系统字体覆盖。

## 清单、资源和安装

```json
{
  "runtime":"ouse-core", "api":1,
  "id":"user.example", "name":"Example",
  "names":{"zh_CN":"示例", "en":"Example"},
  "icon":"icon.bin",
  "capabilities":{"background":false,"notifications":true},
  "assets":[
    {"id":"cover","type":"image","path":"assets/cover.png"},
    {"id":"words","type":"text","path":"assets/zh_CN.json"},
    {"id":"sample","type":"binary","path":"assets/sample.dat"}
  ]
}
```

清单没有 format 字段。ID 使用 `user.*`，最长 47 个 ASCII 字节；名称为 1～47 个 UTF-8 字节，中英文名称可分别省略并回退到 name。源图标可为 icon.png 或预转换 icon.bin，尺寸最大 96×96。

assets 可省略，最多 16 项，每项仅 id/type/path。资源 ID 为唯一的 `[A-Za-z][A-Za-z0-9_-]{0,30}`；路径为 `assets/` 下最多 95 字节的唯一 ASCII 相对路径，段内仅字母、数字、`.`、`_`、`-`，禁止空段、`.`、`..`、反斜杠和冒号。资源限制：

| 类型 | 每项 | 累计 |
| --- | --- | --- |
| image | OIC1＋BGRA8888，宽、高各 1～512 | 图片像素总计 128 KiB，不含启动器图标 |
| text | 无 NUL 的 UTF-8，最多 16 KiB，可为空 | 与 binary 合计 128 KiB |
| binary | 任意字节，最多 64 KiB，可为空 | 与 text 共用总量 |

源 PNG 在 pack 时转换为同目录同名 `.bin`，同时改写包内清单，源文件不变；需要当前 Python 的 Pillow。使用预转换 OIC1 图标和图片时只需 Python 标准库。BPK 恰好包含四个固定文件和已声明资源，全部 ZIP_STORED；整包最多 524288 字节，清单最多 8192 字节，页面、脚本各最多 32768 字节。

```powershell
python tools/bpk.py pack apps/resource-monitor dist/bpk/resource-monitor.bpk
python tools/bpk.py --port COM10 --baud 921600 install dist/bpk/resource-monitor.bpk
```

安装需要 pyserial，关闭占用同一串口的监视器。当前固件和工具默认 921600；工具参数须与固件一致。安装只更新 BPK，不烧录固件或擦除 NVS。VS Code/Cursor 扩展可选择 115200/921600，默认 921600。

```javascript
const resources = Ouse.assets.list();
const words = JSON.parse(Ouse.assets.readText("words"));
const raw = new Uint8Array(Ouse.assets.readBytes("sample"));
Ouse.ui.setImage("coverView", "cover");
```

资源已在实例内存中，三个 assets 方法同步返回。list 的 image 项为 `{id,type,bytes,width,height}`，bytes 是像素字节数；text/binary 为 `{id,type,bytes}`，bytes 是文件长度。readText 仅接受 text；readBytes 接受 text/binary，返回独立 ArrayBuffer；image 由页面 `image.src` 或 setImage 使用。setImage 正常返回 undefined；错误资源 ID/类型抛异常。包资源只读。

## dp、sp、控件和主题

```json
{
  "api":1, "units":"dp",
  "translations":{
    "zh_CN":{"title":"示例","home":"回到桌面"},
    "en":{"title":"Example","home":"Home"}
  },
  "root":{
    "type":"container","id":"page","width":"fill","height":"fill",
    "layout":"column","padding":16,"gap":12,"scroll":true,
    "children":[
      {"type":"toolbar","id":"title","width":"fill","height":56,"textKey":"title"},
      {"type":"image","id":"coverView","width":96,"height":96,"src":"cover"},
      {"type":"button","id":"home","width":"fill","height":"content","textKey":"home"}
    ]
  }
}
```

布局 dp 换算为 `px=dp*densityDpi/160`，fontSize 使用 sp，另乘 fontPercent/100 后选择最近的预生成字号。逻辑 DPI 由板级配置决定（80～320，默认 160），不等于物理 DPI；当前板应用区域为 320×416 dp。系统字号为 100%、115%、130%，切换即时生效。字体池与中文覆盖见 [字体说明](../docs/0.4.13-fonts.md)。

19 种控件包括 container/card/list/listItem/toolbar/divider、label/image、button/slider/textInput/switch/checkbox/dropdown、progressBar/spinner/arc、dialog/bottomSheet。容器支持 row/column、padding/gap 和纵向 scroll；width 可为数值/fill，height 可为数值/fill，部分控件允许 content。未声明 units 或 units=px 使用数值像素坐标。详细组合控件、弹层和输入法契约见 BPK 说明。

textKey/subtitleKey 绑定当前页面 translations.zh_CN/en，动态文案用 `Ouse.locale.t(key)`。每语言最多 128 项、每值最多 256 UTF-8 字节；未知 key 返回 key。每分组 onChange 注册新函数会替换旧函数，null 取消；初始化时自行 get，注册不会立即回调。

`Ouse.ui` 提供 setText/getText、setValue/getValue、setImage、on、setStyle、focus/blur、show/hide/isVisible、setEnabled。on 的事件为 click/change/focus/blur/submit/dismiss；设置值不合成用户 change。样式增量合并，支持 pressed/checked/disabled；颜色可引用系统 `$surface`、`$text`、`$accent` 等 token，随主题变化。

```javascript
Ouse.ui.on("home", "click", () => Ouse.home());
Ouse.ui.setStyle("title", {backgroundColor:"$surface", textColor:"$text"});
Ouse.locale.onChange(() => refreshDynamicText());
Ouse.theme.onChange(theme => console.log(theme.mode));
Ouse.display.onChange(display => console.log(String(display.fontPercent)));
```

## 私有数据与用量

下面六个方法全部返回 Promise，当前应用的 key 与其他应用隔离：

| 方法 | resolve 值 |
| --- | --- |
| `Ouse.storage.get(key)` | 已保存 JSON 值，不存在为 null |
| `Ouse.storage.set(key,value)` | 成功提交为 true |
| `Ouse.storage.remove(key)` | 删除成功 true，不存在 false |
| `Ouse.storage.keys()` | 按 ASCII 字节顺序排序的 key 数组 |
| `Ouse.storage.clear()` | 删除的 key 数量 |
| `Ouse.storage.info()` | `{usedBytes,quotaBytes,keyCount,keyLimit,valueLimit}` |

key 为 1～31 个 ASCII 字符，首位字母或数字，其余字母、数字、`.`、`_`、`-`。每应用最多 32 个 key、单值序列化 UTF-8 最多 8192 字节、全部已提交值合计 64 KiB。set 在提交时以标准 JSON.stringify 生成快照：NaN/Infinity 变 null，数组 undefined 变 null，对象 undefined 属性省略；顶层 undefined/函数、循环、BigInt 无法序列化会 reject。错误和队列满也会 reject，应及时安装 catch。每实例最多 8 个在途存储请求。

```javascript
async function loadPreferences() {
    try {
        const value = await Ouse.storage.get("preferences");
        console.log(JSON.stringify(value));
    } catch (error) {
        console.log(String(error.code || error.message));
    }
}
loadPreferences();
```

原生任务完成文件 I/O，不阻塞 JS 或 UI；set resolve 表示该值已同步并提交文件替换，多 key 不构成事务。普通关闭、同 ID 包更新及设备重启保留私有数据；卸载会排空此前接受的写入，再删除该应用全部私有数据。挂起时 I/O 继续，Promise 回调等恢复或启用后台执行后投递。关闭会释放 resolver，已接受的原生操作仍可安全完成。

`Ouse.system.storage()` 同步读取缓存 `{ready,revision,totalBytes,usedBytes,freeBytes,packageBytes,privateBytes,packageCount,privateApps}`。total/used/free 是 LittleFS 数据分区报告的物理占用，包含块和元数据；packageBytes/privateBytes 是包文件/已提交 JSON 的逻辑长度，二者之和不等于 usedBytes。privateApps 只计至少有一个 key 的应用。原生操作和包变更后刷新缓存；单应用配额用异步 storage.info 查询。整颗 Flash 的容量读 system.info().flashBytes。

## 生命周期与执行边界

首次创建执行 app.js 顶层代码，不调用 onResume。可声明 `globalThis.app={onPause(){},onResume(){},onClose(){}}` 并订阅 `Ouse.lifecycle.onChange(fn)`；`get()` 返回 foreground/background/suspended 等状态。Ouse.home 返回桌面并保留实例，最近应用关闭卡片释放 UI、JS 与计时器。

未声明或未启用后台运行时，离开前台冻结 JS 和定时器，恢复保留剩余延迟，不补跑遗漏周期。需要继续运行须在清单声明 background:true，再调用 `Ouse.background.begin()`，结束调用 end()；通知 ongoing 不会自动开启后台。主题、语言等原生视觉即时更新，JS 通知对挂起实例合并后补发。

每应用独立 QuickJS-NG 0.14.0 环境，JS 堆 512 KiB，QuickJS 栈限 40 KiB；共享 JS 工作任务栈 64 KiB。前台片段预算 12 ms、后台 4 ms，无法抢占任意原生 C 调用。每片段最多处理 8 个 Promise job，剩余延后，不因还有微任务直接报错。页面最多 96 节点、深度 8，节点 ID 最多 31 字节；每应用 8 个计时器，延迟/周期 100～60000 ms。未处理拒绝、死循环、内存超限等进入原生错误页，仍可回桌面关闭应用。

## 系统服务

| 分组 | 方法 |
| --- | --- |
| theme | get、set(light/dark)、onChange |
| locale | get、set(zh_CN/en)、t、onChange |
| display | get、setFontPercent(100/115/130)、setBrightness(10..100,save=true)、onChange |
| power | get、setTimeout(0/15/30/60/120/300)、sleep、wake、onChange |
| audio | get、setVolume(0..100)、setMuted、testTone、record(1..3)、playRecording、stop、onChange |
| wifi | get、setEnabled、scan、connect(ssid,password)、disconnect、forget、onChange |
| notifications | publish/update、cancel、list、onOpen，需声明 notifications:true |
| system | info、storage，同步查询；storage 使用原生缓存 |
| timers | setTimeout/setInterval、clear |

设备 get 读取缓存，写入入口返回是否接受；音频/Wi-Fi 的完成用 requestId/completedId/error 或 onChange 判断。应用私有 storage 与上述设备写请求不同，返回 Promise。system.info 的 CPU 字段是频率，RAM 是可分配堆；暂不提供通用网络请求、模块导入、任意文件、进程或蓝牙 API。

Wi-Fi SSID 为 1～32 UTF-8 字节；密码为空（开放网络）、8～63 字节或 64 位十六进制 PSK，仅连接成功后记住，不从快照返回密码。短录音为 16kHz/16bit/单声道，最多 3 秒，只留在 RAM。sleep 关闭背光，首次唤醒触摸不点击应用。

## 功能检查

`tools/test-device-framework.py`、`tools/test-device-phase2.py`、`tools/test-device-phase3.py` 分别检查执行与输入、组合控件与通知、资源与私有存储。设备脚本通过真实触摸事件操作，只创建各自测试 ID，保存串口、结果与截图；第三阶段还覆盖更新保数据、卸载清数据和 pending 操作的清理。脚本存在不表示当前固件已通过，实际结果以对应固件记录及证据为准。
