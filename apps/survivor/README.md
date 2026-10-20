# 荒野生存 / Wild Survivor — V1.0.0

独立 BPK，要求 Native API 41，无需更新固件。原创程序绘制像素角色与武器。

- 纯横屏。进入时播放瞄准环收拢、角色徽章、标题与短加载条，再依次显示菜单按钮；无加载百分比宣称。
- 主菜单只有开始游戏、设置、退出游戏。菜单完成后停止动画定时器。
- 设置提供左／右手摇杆、爆炸特效开关，保存到应用私有存储。关闭爆炸特效仍保留范围伤害。
- 退出停止游戏定时器并返回桌面；系统可保留应用缓存，再次进入重新播放入口动画。
- 摇杆松手停止移动，自动瞄准与射击，枪口使用 32 向图集旋转。
- 8 波，每波 20 秒；经验升级、波间强化、火箭范围伤害、环绕飞剑、三类敌人。
- 性能上限：12 敌人、6 子弹、8 经验物、1 火箭、1 小型爆炸、2 飞剑。50 ms 更新；满经验池合并数值。
- 后台恢复保持暂停；最高击杀记录持久保存。武器体验入口及其特殊模式已移除。

73 个页面节点（含独立入口和菜单，战斗时隐藏）；精灵图集 312×96 BGRA，徽章 52×52，总图片像素 130624 字节，低于 128 KiB 配额。显示版本固定 V1.0.0，versionCode 仅供内部递增安装。

```powershell
python tools/generate-survivor-assets.py
node tools/test-survivor.cjs
python tools/bpk.py pack apps/survivor dist/bpk/survivor.bpk
python tools/bpk.py --port COM10 install dist/bpk/survivor.bpk
```

本地测试覆盖入口、菜单、设置保存、退出重进、横屏、摇杆松手/取消、枪口与弹道、通关/失败、范围伤害/飞剑/强化、后台无 UI 写入和定时器清理。

COM10 真机验证：设置跨 JS 上下文重建保留；右手摇杆实际触摸；暂停/后台恢复；退出到桌面；重新播放入口后回到三按钮菜单；errors=0，菜单 timers=0，无看门狗或重启。最终证据 `.tools/survivor-menu-final-test.log`；图片 `.tools/survivor-intro-designed.jpg`、`.tools/survivor-menu-designed.jpg`、`.tools/survivor-settings.jpg`。

首次三次串口传输超时，旧应用保留；正常重启后分块安装成功，记录 `.tools/survivor-menu-install-reset.log`。本次未发布云端商店。
