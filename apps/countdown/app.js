"use strict";
let leftMs = 0;
let timerId = 0;
let running = false;
let label = "";
let deadlineMs = 0;
let ringing = false;
let ringTimerId = 0;
let toneTask = 0;
const PRESETS = [
    null,
    [30000, "30秒"],
    [60000, "1分钟"],
    [60000, "微波 1分钟"],
    [120000, "微波 2分钟"],
    [180000, "微波 3分钟"],
    [180000, "泡面 3分钟"],
    [300000, "泡面 5分钟"],
    [360000, "半熟蛋 6分钟"],
    [480000, "煮鸡蛋 8分钟"],
    [600000, "煮鸡蛋 10分钟"],
    [720000, "蒸蛋 12分钟"],
    [120000, "热牛奶 2分钟"],
    [1200000, "午休 20分钟"],
    [1800000, "30分钟"],
    [3600000, "1小时"]
];
function pad(n) { return (n < 10 ? "0" : "") + n; }
function formatSeconds(ms) {
    const seconds = Math.max(0, Math.ceil(ms / 1000));
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const remain = seconds % 60;
    if (hours) return hours + ":" + pad(minutes) + ":" + pad(remain);
    return pad(minutes) + ":" + pad(remain);
}
function showTime() { Ouse.ui.setText("remain", formatSeconds(leftMs)); }
function buttons() {
    Ouse.ui.setEnabled("start", !running && !ringing);
    Ouse.ui.setEnabled("pause", running);
    Ouse.ui.setEnabled("resume", !running && !ringing && leftMs >= 1000);
    Ouse.ui.setEnabled("cancel", !ringing && (running || leftMs > 0));
    Ouse.ui.setEnabled("preset", !ringing);
}
function syncBackground() {
    if (running || ringing) {
        if (!Ouse.background.isActive()) Ouse.background.begin("countdown");
    } else if (Ouse.background.isActive()) Ouse.background.end();
}
function clearTimer() {
    if (timerId) { Ouse.timers.clear(timerId); timerId = 0; }
    running = false;
}
function beep() {
    if (!ringing) return;
    const task = toneTask ? Ouse.tasks.get(toneTask) : null;
    if (task && (task.state === "queued" || task.state === "running" || task.state === "cancelling")) return;
    toneTask = Ouse.audio.alert() || 0;
}
function stopRinging() {
    ringing = false;
    if (ringTimerId) Ouse.timers.clear(ringTimerId);
    ringTimerId = 0;
    if (toneTask) Ouse.tasks.cancel(toneTask);
    toneTask = 0;
    Ouse.ui.hide("doneDialog");
    Ouse.notifications.cancel("done");
    buttons();
    syncBackground();
}
function finish() {
    clearTimer();
    leftMs = 0;
    ringing = true;
    showTime();
    buttons();
    syncBackground();
    Ouse.ui.show("doneDialog");
    beep();
    ringTimerId = Ouse.timers.setInterval(beep, 1000);
    Ouse.notifications.publish({ id: "done", title: "时间到", body: label || "计时结束" });
}
function tick() {
    leftMs = Math.max(0, deadlineMs - Ouse.system.info().uptimeMs);
    if (leftMs <= 0) finish();
    else showTime();
}
function start(ms, name) {
    if (ringing || ms < 1000) return;
    clearTimer();
    leftMs = ms;
    label = name;
    running = true;
    deadlineMs = Ouse.system.info().uptimeMs + leftMs;
    showTime();
    buttons();
    Ouse.notifications.cancel("done");
    timerId = Ouse.timers.setInterval(tick, 1000);
    syncBackground();
}
function whole(text, max) {
    if (text === "") return 0;
    const value = Number(text);
    if (!Number.isInteger(value) || value < 0 || value > max) return null;
    return value;
}
function selectedPreset() {
    const index = Ouse.ui.getValue("preset");
    return index > 0 && index < PRESETS.length ? PRESETS[index] : null;
}
Ouse.ui.on("preset", "change", () => {
    if (running || ringing) return;
    const item = selectedPreset();
    if (!item) return;
    leftMs = item[0];
    label = item[1];
    showTime();
    buttons();
});
Ouse.ui.on("start", "click", () => {
    const minutesText = Ouse.ui.getText("minutes").trim();
    const secondsText = Ouse.ui.getText("seconds").trim();
    if (minutesText !== "" || secondsText !== "") {
        const minutes = whole(minutesText, 180);
        const seconds = whole(secondsText, 59);
        if (minutes === null || seconds === null) return;
        const total = minutes * 60 + seconds;
        start(total * 1000, total + " 秒");
        return;
    }
    const item = selectedPreset();
    if (item) start(item[0], item[1]);
});
Ouse.ui.on("pause", "click", () => {
    if (!running) return;
    leftMs = Math.max(0, deadlineMs - Ouse.system.info().uptimeMs);
    if (!leftMs) { finish(); return; }
    clearTimer();
    showTime();
    buttons();
    syncBackground();
});
Ouse.ui.on("resume", "click", () => {
    if (running || ringing || leftMs < 1000) return;
    running = true;
    deadlineMs = Ouse.system.info().uptimeMs + leftMs;
    timerId = Ouse.timers.setInterval(tick, 1000);
    buttons();
    syncBackground();
});
Ouse.ui.on("cancel", "click", () => {
    if (ringing) return;
    clearTimer();
    leftMs = 0;
    showTime();
    buttons();
    syncBackground();
    Ouse.notifications.cancel("done");
});
Ouse.ui.on("confirmDone", "click", stopRinging);
Ouse.notifications.onOpen(() => { if (ringing) Ouse.ui.show("doneDialog"); });
globalThis.app = {
    onResume() { if (ringing) Ouse.ui.show("doneDialog"); },
    onClose() {
        clearTimer();
        stopRinging();
        if (Ouse.background.isActive()) Ouse.background.end();
    }
};
buttons();
