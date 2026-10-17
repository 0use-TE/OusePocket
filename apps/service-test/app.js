"use strict";
let requestId = 0, current = "home", urlDraft = "https://example.com/", lastResult = "", networkResult = "";
const fields = {
    home: ["go_overview", "go_network", "go_media", "go_alarms", "go_usb"],
    overview: ["refresh", "health", "stopActivity", "clock", "caps"],
    network: ["url", "get", "cancel", "network"],
    media: ["url", "mediaAsset", "mediaUrl", "pause", "resume", "stopMedia", "media"],
    alarms: ["alarm", "dismiss", "snooze", "disableAlarm", "enableAlarm", "remove", "alarms"],
    usb: ["usb"]
};
function show(id, text) {
    if (id !== "result" && !fields[current].includes(id)) return;
    let clipped = "", bytes = 0;
    for (const character of String(text)) {
        const point = character.codePointAt(0), size = point < 128 ? 1 : point < 2048 ? 2 : point < 65536 ? 3 : 4;
        if (bytes + size > 256) break;
        bytes += size;clipped += character;
    }
    Ouse.ui.setText(id, clipped);
}
function result(text) { lastResult = String(text); show("result", lastResult); }
function refresh() {
    show("result", lastResult);
    if (current === "overview") {
        const health = Ouse.system.health(), activities = Ouse.system.activities();
        show("health", "核心状态 " + health.phase + (health.blocker ? " · " + health.blocker : "") + "\n" + health.services.map(service => service.name + ": " + service.state).join(" · ") + "\n本应用可见活动 " + activities.length);
        const clock = Ouse.clock.get(), caps = Ouse.system.capabilities();
        show("clock", "UTC " + new Date(clock.utcMs).toISOString() + "\n时间有效 " + clock.valid + " · 本次已校时 " + clock.syncedThisBoot);
        show("caps", "网络 " + caps.network.available + " · 响应上限 " + caps.network.responseBodyMax + " B\n闹钟 " + caps.alarms.available + " · 每应用 " + caps.alarms.perAppMax + " 个");
    } else if (current === "usb") {
        const usb = Ouse.usb.get();
        show("usb", "USB OTG " + usb.available + " · 已配置 " + usb.mounted + " · 暂停 " + usb.suspended + "\n声卡运行 " + usb.audioRunning + " · 麦克风请求 " + usb.microphoneRequested + "\n网络共享 " + usb.networkUp + " · 丢包 " + usb.droppedPackets + " · 音频欠载 " + usb.underruns);
    } else if (current === "media") {
        const media = Ouse.media.get();
        show("media", media ? "媒体 #" + media.taskId + " · " + media.state + "\n" + media.positionMs + " / " + media.durationMs + " ms · 缓冲 " + media.bufferedBytes + " B" : "当前应用没有媒体会话");
    } else if (current === "alarms") {
        const alarms = Ouse.alarms.list();
        show("alarms", alarms.length ? alarms.map(alarm => alarm.id + " · " + (alarm.ringing ? "响铃" : alarm.missed ? "已错过" : alarm.enabled ? "待触发" : "已结束") + (alarm.nextMs ? "\n" + new Date(alarm.nextMs).toISOString() : "")).join("\n") : "当前应用没有闹钟");
    } else if (current === "network") show("network", networkResult || "填写地址后提交请求");
}
function submitted(id) { result(id ? "已接受任务 #" + id : "提交被拒绝，请检查服务状态与容量"); }
function bind(id, callback, event = "click") {
    if (!fields[current].includes(id) && id !== "back") return;
    const page = current;
    Ouse.ui.on(id, event, () => {
        if (current !== page) return;
        try { callback(); } catch (error) { result(error.name + ": " + error.message); }
    });
}
function bindPage() {
    if (current === "home") {
        for (const page of ["overview", "network", "media", "alarms", "usb"]) bind("go_" + page, () => Ouse.navigation.push(page));
        return;
    }
    bind("back", () => Ouse.navigation.back());
    if (fields[current].includes("url")) {
        Ouse.ui.setText("url", urlDraft);
        bind("url", () => { urlDraft = Ouse.ui.getText("url"); }, "change");
    }
    bind("refresh", refresh);
    bind("stopActivity", () => {
        const activity = Ouse.system.activities().find(activity => activity.kind === "native" && activity.cancellable && !activity.stopping);
        result(activity ? "停止请求 " + Ouse.system.stopActivity(activity.taskId) + "；以任务终态确认" : "没有可停止的本应用原生任务");
    });
    bind("get", () => {
        urlDraft = Ouse.ui.getText("url");
        const request = Ouse.net.request({url: urlDraft, timeoutMs: 10000});
        requestId = request.taskId || 0;
        const id = requestId;
        networkResult = "请求 #" + id;refresh();
        request.then(response => {
            if (requestId !== id) return;
            networkResult = "HTTP " + response.status + " · " + response.bodyBytes + " B\n" + response.body.slice(0, 160);
            if (current === "network") refresh();
        }, error => {
            if (requestId !== id) return;
            networkResult = error.name + ": " + error.message;
            if (current === "network") refresh();
        });
    });
    bind("cancel", () => { networkResult = "取消请求 " + Boolean(requestId && Ouse.tasks.cancel(requestId)) + "；以任务终态确认停止";refresh(); });
    bind("mediaAsset", () => { submitted(Ouse.media.play({asset: "tone"}));refresh(); });
    bind("mediaUrl", () => { urlDraft = Ouse.ui.getText("url");submitted(Ouse.media.play({url: urlDraft}));refresh(); });
    bind("pause", () => { result("暂停 " + Ouse.media.pause());refresh(); });
    bind("resume", () => { result("继续 " + Ouse.media.resume());refresh(); });
    bind("stopMedia", () => { result("停止请求 " + Ouse.media.stop());refresh(); });
    bind("alarm", () => {
        const clock = Ouse.clock.get();
        if (!clock.valid) { result("时间尚未有效，先连接 Wi-Fi 完成校时");return; }
        submitted(Ouse.alarms.set({id: "probe", title: "系统服务测试", atMs: clock.utcMs + 10000, payload: "alarm-probe"}));
    });
    bind("dismiss", () => submitted(Ouse.alarms.dismiss("probe")));
    bind("snooze", () => submitted(Ouse.alarms.snooze("probe", 1)));
    bind("remove", () => submitted(Ouse.alarms.remove("probe")));
    bind("disableAlarm", () => submitted(Ouse.alarms.setEnabled("probe", false)));
    bind("enableAlarm", () => submitted(Ouse.alarms.setEnabled("probe", true)));
}
Ouse.navigation.onChange(event => { current = event.page;bindPage();refresh(); });
Ouse.tasks.onComplete(task => { result("#" + task.id + " " + task.kind + "/" + task.operation + " → " + task.state + "\n" + task.error);refresh(); });
globalThis.app = {onResume: refresh};
bindPage();
refresh();
