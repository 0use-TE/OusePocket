# 跳跳庄园 0.6.0

全屏平台冒险。Pixel Frog CC0 图集用于游戏、动态果园主菜单及三张关卡预览卡；不额外打包大型背景图。

- 主菜单：动态云朵、果树、水果、角色呼吸/弹跳和草地；无英文副标题和宣传小字。
- 选关：三张不同地形与配色的卡片，显示关卡名称、难度与已存档旗帜。
- 与荒野生存共用 tools/app-shared/game-runtime.js，25 FPS 上限、单个待处理帧。物理独立按 60 Hz 运算。
- 保留三张大地图、二段跳、透明图标控制、双轴镜头及每关旗子存档。存储 ID user.pixel-adventure 和既有键不变。
- 原创离线合成背景音乐和六种音效，暂停和后台停播。要求 Native API 47。

构建：python tools/build-pixel-adventure.py
行为检查：node tools/test-pixel-adventure.cjs
产物：dist/apps/跳跳庄园.bpk

引擎：engine.js；视图与控制：game.js；动态菜单：menu.js；音频：audio.js。
素材授权与来源见 ASSETS-LICENSE.txt、provenance.json；音频记录见 AUDIO.md。

游戏内角落显示实测 FPS，每秒统计实际游戏更新/绘制回调；不是屏幕扫描刷新率。暂停或后台后重置统计，首次采样前显示 -- FPS。
