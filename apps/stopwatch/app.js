"use strict";
let elapsedMs = 0;
let timerId = 0;
let running = false;
function pad(n) { return (n < 10 ? "0" : "") + n; }
function formatWatch(ms) {
    const total = Math.max(0, ms);
    const tenths = Math.floor(total / 100) % 10;
    const seconds = Math.floor(total / 1000);
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const remain = seconds % 60;
    const body = pad(minutes) + ":" + pad(remain) + "." + tenths;
    return hours ? hours + ":" + body : body;
}
function showTime() { Ouse.ui.setText("elapsed", formatWatch(elapsedMs)); }
function buttons() {
    Ouse.ui.setEnabled("start", !running && elapsedMs === 0);
    Ouse.ui.setEnabled("pause", running);
    Ouse.ui.setEnabled("resume", !running && elapsedMs > 0);
    Ouse.ui.setEnabled("reset", running || elapsedMs > 0);
}
function syncBackground() {
    if (running) {
        if (!Ouse.background.isActive()) Ouse.background.begin("stopwatch");
    } else if (Ouse.background.isActive()) Ouse.background.end();
}
function clearTimer() {
    if (timerId) { Ouse.timers.clear(timerId); timerId = 0; }
    running = false;
}
function tick() {
    elapsedMs += 100;
    showTime();
}
function run() {
    if (running) return;
    running = true;
    timerId = Ouse.timers.setInterval(tick, 100);
    buttons();
    syncBackground();
}
Ouse.ui.on("start", "click", () => { if (elapsedMs === 0) run(); });
Ouse.ui.on("pause", "click", () => {
    if (!running) return;
    clearTimer();
    buttons();
    syncBackground();
});
Ouse.ui.on("resume", "click", run);
Ouse.ui.on("reset", "click", () => {
    clearTimer();
    elapsedMs = 0;
    showTime();
    buttons();
    syncBackground();
});
globalThis.app = {
    onClose() {
        clearTimer();
        if (Ouse.background.isActive()) Ouse.background.end();
    }
};
buttons();
