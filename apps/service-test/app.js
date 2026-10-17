"use strict";
let requestId = 0;
function show(id, text) { Ouse.ui.setText(id, text); }
function refresh() {
    const health = Ouse.system.health(), activities = Ouse.system.activities();
    show("health", "核心状态 " + health.phase + (health.blocker ? " · " + health.blocker : "") + "\n" + health.services.map(s => s.name + ": " + s.state).join(" · ") + "\n本应用可见活动 " + activities.length + (health.activitiesTruncated ? "（系统活动列表已截断）" : ""));
    const clock = Ouse.clock.get(), caps = Ouse.system.capabilities();
    show("clock", "UTC " + new Date(clock.utcMs).toISOString() + "\n时间有效 " + clock.valid + " · 本次已校时 " + clock.syncedThisBoot);
    show("caps", "网络 " + caps.network.available + " · 响应上限 " + caps.network.responseBodyMax + " B\n闹钟 " + caps.alarms.available + " · 每应用 " + caps.alarms.perAppMax + " 个");
    const usb = Ouse.usb.get();
    show("usb", "USB OTG " + usb.available + " · 已配置 " + usb.mounted + " · 暂停 " + usb.suspended + "\n声卡运行 " + usb.audioRunning + " · 麦克风请求 " + usb.microphoneRequested + "\n网络共享 " + usb.networkUp + " · 丢包 " + usb.droppedPackets + " · 音频欠载 " + usb.underruns);
    const media = Ouse.media.get();
    show("media", media ? "媒体 #" + media.taskId + " · " + media.state + "\n" + media.positionMs + " / " + media.durationMs + " ms · 缓冲 " + media.bufferedBytes + " B" : "当前应用没有媒体会话");
    const alarms = Ouse.alarms.list();
    show("alarms", alarms.length ? alarms.map(a => a.id + " · " + (a.ringing ? "响铃" : a.missed ? "已错过" : a.enabled ? "待触发" : "已结束") + (a.nextMs ? "\n" + new Date(a.nextMs).toISOString() : "")).join("\n") : "当前应用没有闹钟");
}
function submitted(id) { show("result", id ? "已接受任务 #" + id : "提交被拒绝，请检查服务状态与容量"); }
Ouse.ui.on("refresh", "click", refresh);
Ouse.ui.on("stopActivity", "click", () => {
    const activity = Ouse.system.activities().find(a => a.kind === "native" && a.cancellable && !a.stopping);
    show("result", activity ? "停止请求 " + Ouse.system.stopActivity(activity.taskId) + "；以任务终态确认" : "没有可停止的本应用原生任务");
});
Ouse.ui.on("get", "click", () => {
    const request = Ouse.net.request({url: Ouse.ui.getText("url"), timeoutMs: 10000});
    requestId = request.taskId || 0;
    show("network", "请求 #" + requestId);
    request.then(response => show("network", "HTTP " + response.status + " · " + response.bodyBytes + " B\n" + response.body.slice(0, 160)), error => show("network", error.name + ": " + error.message));
});
Ouse.ui.on("cancel", "click", () => show("network", "取消请求 " + Boolean(requestId && Ouse.tasks.cancel(requestId)) + "；以任务终态确认停止"));
Ouse.ui.on("mediaAsset", "click", () => { submitted(Ouse.media.play({asset: "tone"})); refresh(); });
Ouse.ui.on("mediaUrl", "click", () => { submitted(Ouse.media.play({url: Ouse.ui.getText("url")})); refresh(); });
Ouse.ui.on("pause", "click", () => { show("result", "暂停 " + Ouse.media.pause()); refresh(); });
Ouse.ui.on("resume", "click", () => { show("result", "继续 " + Ouse.media.resume()); refresh(); });
Ouse.ui.on("stopMedia", "click", () => { show("result", "停止请求 " + Ouse.media.stop()); refresh(); });
Ouse.ui.on("alarm", "click", () => {
    const clock = Ouse.clock.get();
    if (!clock.valid) { show("result", "时间尚未有效，先连接 Wi-Fi 完成校时"); return; }
    submitted(Ouse.alarms.set({id: "probe", title: "系统服务测试", atMs: clock.utcMs + 10000, payload: "alarm-probe"}));
});
Ouse.ui.on("dismiss", "click", () => submitted(Ouse.alarms.dismiss("probe")));
Ouse.ui.on("snooze", "click", () => submitted(Ouse.alarms.snooze("probe", 1)));
Ouse.ui.on("remove", "click", () => submitted(Ouse.alarms.remove("probe")));
Ouse.tasks.onComplete(task => { show("result", "#" + task.id + " " + task.kind + "/" + task.operation + " → " + task.state + "\n" + task.error); refresh(); });
globalThis.app = {onResume: refresh};
refresh();

Ouse.ui.on("disableAlarm","click",()=>submitted(Ouse.alarms.setEnabled("probe",false)));
Ouse.ui.on("enableAlarm","click",()=>submitted(Ouse.alarms.setEnabled("probe",true)));
