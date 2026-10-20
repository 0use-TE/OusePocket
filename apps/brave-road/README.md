# 勇者之路·本地单人适配 0.4.1

运行目标：OusePocket，Core 0.42.0 / Native API 44。运行重写的 JS，不运行 Flash。当前重点是第一关；不能称为完整原版移植。

## UI 方案

`page.json` 建立 18 个控件：场景 Canvas、HUD Canvas、暂停按钮和菜单。场景每次更新提交显示列表；`tools/app-shared/game-hud.js` 比较显示状态，血量、魔法、经验、等级、击杀数与宽度均未变化时不提交。菜单只在打开、暂停、结束等事件更新文本。布局只在显示尺寸变化时更新。

page 并非每帧重新解析 JSON。需要避免的是每帧修改大量独立对象而产生的 JS/C 调用、布局与失效区域开销。普通 UI 适合菜单；复杂动态场景适合 Canvas。HUD 是否适合 Canvas 取决于复杂度，此处用单独 Canvas 把状态条更新与整个场景分开。

## 第一关已接入的数据

- 原版第一关碰撞矩阵、6 组波次、28 只怪物，数量依次为 3 / 3 / 6 / 3 / 4 / 9。
- l1、l2、l3 各自的 17 帧动画与左右朝向，独立攻击前摇、基础追踪条件、掉落池。
- 怪物 HP / 攻击 / 防御 / 经验：l1 = 100 / 20 / 0 / 5；l2 = 200 / 30 / 0 / 10；l3 = 500 / 50 / 0 / 20。
- 初始人物 HP 300、MP 60、剑攻击 25、盾防御 20。升级经验每级 50，上限 40 级；按原公式计算装备和等级对属性的影响。
- 伤害为 `max(1, floor((攻击-防御) × 倍率 × (10+random)/10))`。普通连击、Lv5 耗魔 10 的技能、受伤保护、药水和经验掉落已接入。
- 原版首关背景音乐完整转码为 8kHz 单声道 PCM16 WAV 并循环。攻击、受击、升级、拾取共 7 个短音效为 16kHz PCM16；背景音乐与最多 5 路音效同时播放。完整胜利/失败音频单独播放一次。
- BGM、胜利、失败音频存 SD；短音效保留在包内。不是截取数秒音乐伪装完整音乐。

数值来源是 `.tools/game-47931/clean-export/scripts/roles/mainrole.as`、`roles/allrole.as`、`roles/enemys/littlerole1.as` 与 `levelDate.as`。怪物配置与地图由准备脚本提取，音频保留来源与 SHA256。

## 仍有差异

这不是动作、AI、画面逐帧等价的复刻。触屏操作采用左侧四向摇杆、右侧独立攻击/跳跃、顶部四技能。原版键盘操作、完整连招判定、空中攻击、冲刺与后续高级技能尚未逐项移植。

人物使用 16 个选定动作帧；精灵图集缩小以满足包预算。首关背景从 SD 加载 1620×385 村庄和 930×330 远景，绘制时为 1:1 像素；缺图时回退包内缩图并提示。盾、衣服、剑的二级掉落会改变数值，尚未动态更换角色外观；匕首掉落只记录，武器切换尚未完成。物理、镜头和寻路为设备重写。清空波次显示胜利，目前不要求走到原版出口。原版剧情、商店、宠物、双人及其他关卡均未包含在本版玩法中。

## 操作与存档

左侧摇杆左右移动、上下爬梯，向下同时拾取。右侧攻击支持按住连击，跳跃单独触发；不再自动近战。顶部旋斩/突进/震地/恢复为触屏适配技能（不是原版四技能逐项复刻），分别耗蓝 10/8/14/16，冷却 4/3/7/9 秒。旋斩需落地且不在其他动作中。菜单支持继续、重新开始与声音开关。存档键 `first-stage-v2` 独立保存等级、经验、装备和声音设置，避免继承旧原型的非原版数值。

## 重建与验证

```powershell
python tools/extract-brave-road.py --java "D:/Software/JetBrains Rider 2026.2.2/jbr/bin/java.exe"
python tools/prepare-brave-road-first.py
node tools/test-brave-road.cjs
python tools/test-sd-images.py
python tools/test-ouse-services.py media wave
python tools/test-canvas-runtime.py
python tools/test-ouse-app-packages.py --apps brave-road
python tools/upload-sd-files.py --port COM10 --source .tools/game-47931/sd-audio --folder Music
python tools/upload-sd-files.py --port COM10 --source .tools/game-47931/sd-images --folder Pictures
```

准备脚本需要本地 FFmpeg：`.tools/media-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe`。`game.js` 和本应用 `hud.js` 合成为设备运行的 `app.js`。旧 `prepare-brave-road.py` 为 0.1 原型，不应用于本版重建。

主机检查覆盖三种怪物数值、全部波次、伤害公式、升级、技能门槛、攻击前摇、跳跃落地、拾取、HUD 不变时不提交、地图绘制边界、音频结束不重播和生命周期。原生测试覆盖混播限幅、5 路限制、资源复制、所有权、暂停、循环和取消。这些检查不代表人工通关和原版行为等价。

资源：8 个图片资源，解码像素 306,976 字节；音效银行 166,358 字节；BPK 约 505 KiB，总包上限仍为 512 KiB。Native API 43 将图片总预算从 128 KiB 调至 512 KiB，单边仍限 512，资源最多 16 项。完整音乐在 SD 卡独立存放。

## 来源与提取

原页面：<https://www.4399.com/flash/47931_1.htm>。原始 SWF SHA256：`3f633169368ff02907553f3145775379c3c7409489f9d9f0f67aa954a5559a6e`。JPEXS FFDec 26.4.0 导出 1459 个图片、413 个脚本、24 个声音文件。反编译结果不是原始 FLA/源码，部分脚本仍有伪栈表达式，不能直接重新编译。

本地归档：`.tools/game-47931/brave-road-extracted.zip`。页面保留作者版权声明；此记录不声称已进入公有领域。本次仅本地提取、适配和设备安装，未公开发布素材。

## 本轮真机结果

COM10 已安装 0.2.0。`.tools/diagnostics/brave-road-v2-verified2` 记录第一组 3 只怪物全部击败、24 次短音效接受、音频错误 0、运行时错误 0。场景累计提交 700 次时，HUD 仅提交 5 次；此计数不是面板 FPS。

`.tools/diagnostics/brave-road-v2-loop` 中媒体已消费播放进度达到 109,888 ms，超过完整首关音乐约 90.04 秒，验证循环后继续输出。关闭后 instances / contexts / widgets / timers / js_heap_bytes 全部为 0，再打开正常。此处依据媒体消费进度与原生混播状态验证，并未声称已进行主观音质评测。

汇总为 `docs/brave-road-device-results.json`。真机只实战验证第一组怪物；没有人工通关首关的结论。早期失败证据 `brave-road-v2-combat` 中是已修复的 SD 根目录路径错误，最终有效结果应以上述两个目录为准。


## 0.3.0 高清背景验证

Core 0.42.0 / Native API 44 和游戏 0.3.0 已安装到 COM10。两张背景均从 SD 加载，启动约 2 秒；实际首组怪物战斗运行时错误为 0，音乐播放进度正常。加载背景时 PSRAM 可用约 19.8 MiB，关闭后恢复约 24.6 MiB，上下文/控件/计时器/JS 堆归零，再打开正常。

真机前后对照在 `.tools/diagnostics/brave-road-hq-combat/before-after.png`，原始记录分别为 `brave-road-hq-verified`、`brave-road-hq-combat` 和 `brave-road-hq-audio`，汇总为 `docs/brave-road-hq-results.json`。包内缩图仅供缺失高清资源时回退。


## 0.4.0 操作适配与修复

血量、魔法、经验放左上，四技能在顶部、暂停在右上。左侧固定摇杆，右侧攻击/跳跃。技能按钮显示蓝耗或剩余冷却，蓝量不足/冷却时禁用。技能冷却只随实际战斗推进，后台与暂停不推进。

修复：后台返回时先暂停而非直接继续受击；输入取消/暂停清除方向、攻击持有和跳跃；存档异步读取收尾不再重建已开始的游戏；首次后台读取完成后延迟到恢复再初始化；怪物跌落重生不再写死 520 宽度且限制世界边界；换衣服时增加对应当前生命；高视口下镜头纵向范围不再出现负上限。突进逐步检测墙体并限制锁定区域，不能穿墙出界。

主机回归（高清/包内回退两种路径）通过：两种横屏视口按钮边界、手动攻击、方向取消、跳跃、技能耗蓝/冷却/范围伤害/恢复、六波敌人、伤害与成长、音频、保存及生命周期。真机检查证据 `.tools/brave-controls-live-test.log`，截图 `.tools/brave-controls-live.jpg`、`.tools/brave-controls-live-skill.jpg`；errors=0，无本轮重启或看门狗告警。尚未人工完整通关，不宣称所有原有差异已修完。

当前输入链路单点触摸，不支持双指同时按摇杆和攻击。四技能是本地适配玩法。完整移植、后续关卡、武器外观/匕首切换等此前差异仍保留。仅本地安装，未发布云端。


## 0.4.1 透明操作与原版快捷技能校正

本节替代上节自定义技能规则：原版 `roles/mainrole.as::fastSkill` 的 1～4 依次为 attack4 / rollattack / skill1 / skill2_1→skill2_2→skill2_3。等级 5/7/15/20，MP 10/20/20/60；前两项要求在地面，没有额外秒数冷却，动作锁期间不能重复释放。

适配重击（第6帧命中、3倍）、回旋（第2帧起持续判定、0.5倍、双侧范围）、冲击（15帧方向移动及命中）、升空/下落/落地三阶段（落地双侧3倍）。保留原版等级/耗蓝/动作时长与缩放后范围；受现有16帧角色图集限制，原版光效、音效及动作帧仍简化，不宣称逐帧一致。

右上四技能用透明圆环及剑/回旋/冲击/落地符号竖排，下面显示解锁等级或蓝耗。左下摇杆与右下攻跳只有轮廓，透明命中按钮负责输入；不透明 UI 底板已移除，数值画布不再绘制整块背景，场景延伸至完整应用区域。技能图标为本地程序绘制，不是声称原版图标素材。

底层 esp_lvgl_adapter 有 MULTI_CONTROL 模式，但 BSP 当前 DEFAULT_CONFIG 为 SINGLE；脚本运行时也仅有一个 pointer 状态。未开启双指，这需要固件输入路由、触点身份、取消以及系统手势联动修复。本轮不更新固件。

主机规则测试通过；安装后操作验证由用户进行，不进行自动点击/移动。

透明图标与数值合并在同一透明 HUD Canvas，沿用固件每页最多两个 Canvas 的约束。最初第三 Canvas 方案在安装校验中被拒绝，旧包保留；未把该次安装计为成功。
