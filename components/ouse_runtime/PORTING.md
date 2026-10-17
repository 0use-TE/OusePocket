# 来源与执行边界

当前 0.9.0 新增任务 ID/终态事件、后台原因与应用/系统诊断；异步音频和 Wi-Fi 接受请求返回任务 ID（拒绝 false），存储 Promise 增加 taskId。执行预算口径已改为任务获调度时间，以下历史说明中的 12/4 ms 不应解释为纯 JS CPU 时间。接口、取消/关闭策略和计时限制统一见 [0.9.0 系统机制](../../docs/0.9.0-kernel.md)。

归档 `legacy/.reference/esp-brookesia/runtime/brookesia_runtime_js/src/backend.cpp` 使用 QuickJS Runtime/Context、内存/栈限制、事件绑定、任务队列和暂停/销毁生命周期。本组件采用相同的 `espressif/quickjs-ng ==0.14.0`，保留每应用独立环境，但没有引入归档的服务、HAL、GUI 文档或线程管理架构。页面和 API 是当前轻量核心自己的契约；探索阶段只维护一种 BPK。

QuickJS 原始版权/许可保留在 managed component 的 LICENSE 和源文件中。运行时只调用 quickjs.h；不初始化 quickjs-libc 的 std/os、文件或进程辅助 API，也不启动 js_std 后台事件循环。

固定 GCC 15 RISC-V 工具链下，QuickJS 组件额外使用 `-fno-strict-aliasing`，保留 `-O2` 和固定的 0.14.0 依赖。该工具链的 uint32_t 是 unsigned long，QuickJS 字符串迭代器却把 uint32_t idx 转成 int 指针交给 string_getc 推进；不同类型的别名写入可能被优化器错误视为不影响 idx。供应组件仅有 `-Wno-strict-aliasing`，它关闭警告而不关闭这项优化。本地覆盖只施加到 QuickJS target，不修改 managed 源码或其他组件的编译选项。

修复前真机最小样例中，for-of 与手动 iterator.next 都把 `320中文😃` 反复读为首字符 `3`，显式 codePoint 索引正常；这会同时导致重复文案与应用截断循环开销增加。相同样例在补充编译选项后的 build4 上已通过真机对照：两种迭代均依次得到 `3/2/0/中/文/😃`，手动迭代到结尾正常返回 DONE，显式索引结果保持正确。证据保存在 `.tools/diagnostics/device-080-string-before/string-result.json` 与 `device-080-string-after/string-result.json`；修复后 ELF SHA-256 为 `258d0f0bbfca176917d234f2488aaacb6ad768d34ca1da52112f15647f0a4e2c`。常规 JS 回调预算保持不变，其他功能验收以版本记录为准。

## 脚本与界面所有权

所有 QuickJS Runtime/Context、JSValue、回调、计时器和 Promise job 都归单一 `ouse_js` FreeRTOS 任务所有。每应用仍有独立 Runtime/Context 和 512 KiB JS 堆限制。共享任务使用 64 KiB 内部 RAM 栈，QuickJS 栈上限 40 KiB；常规前台每轮预算 12 ms、后台 4 ms，每应用一轮至多一个执行片段。上下文与 JSON 文档初始化、每页独立 schema 校验、初始页面模型、隔离编译验证、首次脚本求值分别使用 100 ms 预算；切页的单页模型解析也使用独立 100 ms。这是各阶段的执行限额，不表示整个打开或验证流程在 100 ms 内完成。无可运行实例时任务阻塞等待。

LVGL 任务只创建控件、读取有界纯数据模型、应用脏状态和将输入事件入队，不执行或释放 JSValue。每实例输入队列最多 16 个事件，每轮最多处理 4 个；暂停和恢复推进输入代次，旧页面事件会被丢弃。系统设置操作合并为短 UI 命令，查询读取快照；电源、音频、Wi-Fi 和通知使用线程安全的原生接口。

预算不能抢占任意原生 C 调用。通知绑定只执行有界数据复制和短锁操作，偏好/硬件绑定读取快照或提交短请求。通知点击在本轮预算已耗尽时留到下一轮，不用过期预算调用 JS。

打开应用时复制页面、脚本、ID 和图片数据。关闭时先撤销 UI 定时器、输入法和事件，清理原生控件/图片缓存，再释放 UI 引用；脚本任务随后清理 JS 和自己的引用。编译验证也在该任务处理，但不执行脚本；UI 用 validate_begin 只提交请求，安装任务在 UI 锁外用 validate_wait 最多等待 2 秒，超时后的验证保留自己的源码副本。

## 生命周期

首次创建执行 `app.js` 顶层代码，不调用 `app.onResume()`。初始化应在顶层完成；`onResume()` 仅用于已存在实例从后台恢复。可通过 `globalThis.app` 注册 `onPause`、`onResume`、`onClose`，并通过 `Ouse.lifecycle.onChange(fn)` 观察前台/后台/挂起状态。

默认离开前台后保留上下文并暂停 JS。清单声明 `capabilities.background: true` 后，仍需调用 `Ouse.background.begin()` 才会继续受限执行；`end()` 恢复默认挂起。显式关闭始终销毁上下文。挂起计时器保留剩余时间，恢复后继续计时，不补发后台错过的全部周期。后台 Home、模态显示和输入焦点请求不能抢走当前前台。

原生可靠定时、闹钟和媒体会话尚不由这个机制提供。通知仍然只是系统消息记录，不代表系统自动创建了对应的后台任务。

## 子页面所有权

单 root 契约保持不变，其导航 ID 为 main；多页以 pages/id/root 和 initialPage 声明，最多 16 页、8 层栈，每页独立使用 128 节点上限，整个 page.json 仍限 32 KiB。全部页面 schema 都在隔离编译验证时检查，只有当前页建立原生模型和 LVGL 对象。静态页面定义归当前 QuickJS 环境，不能由应用 JS 修改。

多页校验每页重新计 100 ms，页间让出一个调度 tick，不能让所有页面共用上下文初始化剩余的时间。模型复用仅清理上一页实际使用的节点；schema 失败日志记录应用 ID、阶段、页面 ID、节点数、elapsed 和 timeout，便于区分格式错误与预算耗尽。隔离验证调用者仍最多等待 2 秒。

导航请求在 model_lock 下保存目标并推进输入代次。UI 任务先收起输入法、关闭被 LVGL 移到屏幕层的 dropdown、删除模态和页面对象、丢弃图片缓存，然后确认已分离。脚本任务才释放旧节点 handler、复用模型缓冲解析目标页；UI 完成新树布局并发布尺寸快照后，脚本任务投递 navigation.onChange。切换期间暂停本实例普通回调，onChange 先于延后的 Promise/存储完成/计时器；整个过程不在 UI 任务执行或释放 JSValue。

页面切换保留 JS 上下文、计时器、存储请求和应用生命周期，不触发 onPause/onResume。返回重建默认 UI，不保存滚动或表单状态；应用使用 JS 业务状态和 onChange 恢复。节点监听随离页销毁，旧代次输入不能命中新页同名节点；跨页异步业务回调仍需应用自行检查当前页。原生 Back 在键盘、模态之后优先弹出页面栈，根页才交给桌面。

## 控件、样式与输入

当前共有 19 种控件：`container`、`label`、`button`、`slider`、`image`、`textInput`、`switch`、`checkbox`、`dropdown`、`progressBar`、`spinner`、`arc`、`card`、`list`、`listItem`、`toolbar`、`divider`、`dialog`、`bottomSheet`。组合控件直接由 LVGL 对象和普通子控件实现，不经过 Brookesia 服务管理器。

推荐页面显式声明 `units: "dp"`，统一使用 dp 布局与 sp 字体。基础接口为 `setText/getText`、`setValue/getValue`、`on`、`setStyle`、`focus/blur`、`show/hide/isVisible`、`setEnabled`。事件包含 `click`、`change`、`focus`、`blur`、`submit`、`dismiss`。同一控件/事件只有一个监听器，传 null 可解除。

`setStyle` 增量合并，未提供项保留；颜色支持 `#RRGGBB` 和 `$background`、`$desktop`、`$surface`、`$text`、`$secondary`、`$accent`、`$button`、`$pressed`、`$border`、`$disabled`、`$error`、`$icon`。主题引用在显示和恢复时重新解析，显式颜色不随主题改变。`style.states` 的 pressed/checked/disabled 使用 LVGL 状态选择器；accentColor/thumbColor 对应有效轨道和拇指，开关/复选框的状态背景同时映射实际 indicator；null 颜色或 null 状态移除相应本地覆盖。禁用组同时禁用原生子控件输入，避免只拦截 JS 事件却仍弹出键盘。

`dialog` 和 `bottomSheet` 初始隐藏，显示其中一个会关闭其他模态。背景点击和 Back 先隐藏模型，再将 `dismiss` 原因（backdrop/back）加入脚本事件队列。应用暂停时全部收起，恢复不重开；显式 hide 不派发 dismiss。模态拥有应用 viewport 内的遮罩和子控件，不创建全局悬挂窗口，也不允许模态嵌套。系统 Back 先收键盘，再关闭应用模态，最后执行应用导航。

固件关闭了 LVGL 自带主题，因此控件必须提供完整皮肤，不能只写颜色。原生设置与脚本开关共用同一圆角/白色拇指/动画样式；复选框显式提供勾选符号，dropdown 提供独立列表和符号字体，slider/progress 提供完整轨道，arc/spinner 使用 arc_width/arc_color。符号使用已启用的 Montserrat 16/24，不依赖不含 Font Awesome 的正文/CJK 字体。字体变化会刷新符号字号和文本控件最小高度。

`textInput` 绑定系统单例输入法和自己的 viewport，支持 resize/pan/overlay；键盘和页面尺寸变化由 display 快照及 onChange 提供。当前输入模式为英文/符号、数字、密码，不含中文拼音词库。文本控件的 textKey、列表项的 subtitleKey 使用 BPK 自己的中英文词条。

## 通知所有权

`capabilities.notifications: true` 授权当前应用使用 `Ouse.notifications.publish/update/cancel/list/onOpen`。原生服务只保存有界字符串、时间和标志，不保存 JS 值、回调或 LVGL 指针。publish/update 的身份始终是实例复制的 app ID 加应用自己提供的通知 ID，应用无法覆盖其他应用记录。

点击通知先将不可变 ID/payload 放入原生待投递队列，再打开所属应用；前台脚本安装 onOpen 监听后才消费，回调成功才确认。已关闭应用重新执行顶层代码也能安装监听并收到事件；没有监听时事件保留至过期。普通关闭保留通知记录；替换/卸载清理该应用记录和待投递事件。发布/取消在 model_lock 下检查 closed 并完成短存储操作，避免旧实例在卸载之后重新发布通知。

通知记录存在 RAM，不跨重启持久化。应用最多 6 条、全系统最多 24 条；普通记录可单条清除或清除全部，ongoing 只能由应用取消或改为普通记录。待点击事件全局 8 个、每应用 4 个，未投递事件 5 分钟后过期。

## 包内资源与异步私有存储

运行时接收图片描述符和 text/binary 资源，合计最多 16 个，资源 ID 是字母开头、最多 31 字节的 ASCII 字母/数字/下划线/连字符。图片必须为已验证的 ARGB8888 描述符，宽高 1～512、有效 stride 和精确像素长度，累计最多 128 KiB。text/binary 累计另有 128 KiB；text 每项最多 16 KiB，必须为有效 UTF-8 且无 NUL，binary 每项最多 64 KiB，可为空。BPK 解析器验证文本编码；直接构造 C 包的调用者也必须满足此前置条件。

打开与隔离编译验证均持有资源的完整副本，优先放入 PSRAM。每个实例持有自己的 ID、描述符、像素和数据；关闭时先删除 UI 图片及缓存，再让引用计数释放数据，替换/卸载不会留下包内借用指针。

`Ouse.assets.list()` 同步返回 `{id,type,bytes,width?,height?}` 数组，图片的 bytes 是像素数据大小，不含 OIC1 文件的 8 字节头。`readText(id)` 返回只读 text 的字符串；`readBytes(id)` 为 text/binary 创建独立 ArrayBuffer 副本，不暴露图片像素。`Ouse.ui.setImage(nodeId,imageId)` 只修改纯数据中的资源 ID 和版本，界面任务之后调用 LVGL 更换源。

`Ouse.storage.get/set/remove/keys/clear/info` 全部返回 Promise，包括参数错误也以拒绝传递。key 为 1～31 个 ASCII 字符，首字符字母/数字，后续可用字母、数字、点、下划线和连字符。当前应用 ID 由原生实例固定，应用不能指定其他应用的目录。

set 使用标准 JSON.stringify：编码后不含终止 NUL 的 JSON 限 1～8192 字节。顶层 undefined/函数、循环引用、BigInt 等失败会拒绝；NaN/Infinity 变为 null，数组中的 undefined 变为 null，对象中的 undefined 字段省略。get 不存在的 key 返回 null，set 成功返回 true，remove 返回是否存在并删除，keys 返回排序后的字符串数组，clear 返回删除键数量。info 返回 usedBytes/quotaBytes/keyCount/keyLimit/valueLimit（配额 65536、键上限 32、值上限 8192）。

实际 LittleFS 读写、扫描和原子替换全部归原生存储任务。JS 任务只序列化、非阻塞提交和处理完成结果，不执行文件 I/O。提交及 closed/faulted 检查在同一 model_lock 中，关闭后旧脚本不能再排入写请求。卸载方关闭注册实例后，通过原生 FIFO purge 屏障清理已接受的请求和数据。

每实例最多 8 个未结束请求，全局最多保留 64 个请求（包括已关闭实例尚未返回的请求），完成队列有对应的 64 个指针槽位。每请求持一个实例引用；原生回调只转移 malloc JSON 结果、入队并唤醒脚本任务，不调用 JS 或 LVGL。脚本任务始终接收完成消息，挂起实例只保留纯数据结果，不 settle Promise；前台或主动后台运行后，才在受控执行片段内 resolve/reject 和处理后续微任务。关闭/异常立即撤销 JS resolver，稍后到达的原生回调仍能安全释放请求、结果和最后的实例引用。

`Ouse.system.storage()` 同步复制原生缓存统计，返回 ready/revision/totalBytes/usedBytes/freeBytes/packageBytes/privateBytes/packageCount/privateApps，不扫描目录或访问 Flash；统计会在原生操作完成后刷新。本文描述源码契约，真机和构建结果由对应版本记录提供。

## 有界限制

- 每页最多 128 个 JSON 节点；根节点深度为 0，允许到深度 8；ID 最多 31 个 UTF-8 字节且在该页唯一。最多 16 页，导航栈最多 8 层。
- 页面、脚本各最多 32,768 字节；文本和词条值最多 256 个 UTF-8 字节。中文/英文词表各最多 128 条。
- 每应用 8 个计时器，周期/延迟 100～60,000 ms；每执行片段最多处理 8 个 Promise job，未完成的微任务延至下一片段，默认挂起时冻结。单个同步 job 仍受当前片段的中断预算约束，常规前台为 12 ms、后台为 4 ms，首次脚本求值阶段为 100 ms；未处理拒绝在队列清空后结算，仍保留 8 个拒绝跟踪槽和 512 KiB 堆上限。
- 输入最大长度可设为 1～256，默认 128；下拉选项最多 24 项，总选项字符串最多 512 字节。
- 包内图片与 text/binary 合计最多 16 项，复制像素累计最多 128 KiB，数据累计另有 128 KiB；单 text 16 KiB、单 binary 64 KiB。
- 通知 ID 最多 47 字节、title 127 字节、body 383 字节、payload 255 字节，均为 UTF-8 字符串；ID 和 title 必须非空。

上述是源码契约与边界，不替代对应版本的构建和真机验证记录。详细 JSON/JS 用法见 `docs/bpk.md`。
