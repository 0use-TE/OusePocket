"use strict";
let stage = "";
let session = 0;
let token = 0;
let playing = false;
let frame = 0;
const frames = ["disc0", "disc1", "disc2", "disc3"];
function status(text) { Ouse.ui.setText("status", text); }
function now(text) { Ouse.ui.setText("now", text); }
function spin(on) {
    playing = !!on;
    now(playing ? "播放中" : "已暂停");
    Ouse.ui.setText("toggle", playing ? "暂停" : "播放");
}
function tap(usage) {
    if (!session) { status("未连接"); return false; }
    const ok = Ouse.bluetooth.consumer(usage);
    if (!ok) return false;
    const mine = ++token;
    Ouse.timers.setTimeout(function () { if (mine === token) Ouse.bluetooth.release(); }, 100);
    return true;
}
function arm() {
    session = 0;
    if (Ouse.permissions.get("bluetooth") !== "granted") {
        stage = "bluetooth";
        Ouse.permissions.request("bluetooth");
        return;
    }
    if (Ouse.permissions.get("hid") !== "granted") {
        stage = "hid";
        Ouse.permissions.request("hid");
        return;
    }
    stage = "open";
}
Ouse.ui.on("previous", "click", function () { if (tap(182)) spin(true); });
Ouse.ui.on("toggle", "click", function () { if (tap(205)) spin(!playing); });
Ouse.ui.on("next", "click", function () { if (tap(181)) spin(true); });
Ouse.ui.on("mute", "click", function () { tap(226); });
Ouse.ui.on("quieter", "click", function () { tap(234); });
Ouse.ui.on("louder", "click", function () { tap(233); });
Ouse.timers.setInterval(function () {
    if (session) return;
    if (stage === "bluetooth") {
        if (Ouse.permissions.get("bluetooth") !== "granted") return;
        stage = "hid";
        Ouse.permissions.request("hid");
        return;
    }
    if (stage === "hid") {
        if (Ouse.permissions.get("hid") !== "granted") return;
        stage = "open";
    }
    if (stage !== "open") return;
    const result = Ouse.bluetooth.open("hid");
    if (result) {
        session = result;
        status("已连接");
        spin(true);
    } else status("连接占用");
}, 200);
Ouse.timers.setInterval(function () {
    if (!playing || !session) return;
    frame = (frame + 1) % frames.length;
    Ouse.ui.setImage("disc", frames[frame]);
}, 140);
arm();
globalThis.app = {
    onPause: function () { Ouse.bluetooth.close(); session = 0; playing = false; },
    onResume: function () { status("连接中"); now("连接中"); arm(); },
    onClose: function () { Ouse.bluetooth.close(); session = 0; playing = false; }
};
