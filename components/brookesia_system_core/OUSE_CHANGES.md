# Ouse 产品扩展

基于官方快照 e937455b0db1a3e873b1da61d6d13652f3dcc7e2，Core 0.8.4。

- 将本地依赖路径指向 `.reference/esp-brookesia`。
- 应用使用 Stack 挂载模式；pause 隐藏当前 screen-flow 根，resume 恢复并通知前台变化，start 已暂停应用时转为 resume。
- pause/resume 检查生命周期状态；重复pause幂等，Running应用重新激活仅恢复显示和前台信息，不重复调用on_resume；找不到界面根时报告错误。
- 新增 set_app_visible、send_app_action 与 on_app_navigation_home。
- 统一返回动作 system.navigation.back，只由产品桌面派发给前台；根页面可请求 navigation.home。
- 在隐藏应用前调用默认无操作的 `on_app_snapshot` 钩子，由产品实现截图；不将 LVGL 截图策略放入通用 Core。
- STOP 成功后卸载运行时环境，PAUSE 保留；JS 应用再次 START 使用新环境，避免全局 let 重复声明。
- START 生命周期成功后，显式恢复当前内容页及标题栏可见性；预加载DOM曾在PAUSE时隐藏、STOP后继续缓存，不会再因重新挂载保留hidden而显示空白。恢复失败走现有启动回滚。输出应用ID、当前screen及visible诊断日志。

不提供进程隔离，也不强制暂停应用自建任务。详细契约见 docs/08-files-power-navigation.md。
