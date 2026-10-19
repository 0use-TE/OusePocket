"use strict";
let stage = "";
let session = 0;
let token = 0;
let playing = false;
let frame = 0;
const frames = ["disc0", "disc1", "disc2", "disc3"];
function status(text) { Ouse.ui.setText("status", text); }
function spin(on) {
    playing = !!on;
}
function tap(usage) {
    const link=Ouse.bluetooth.get();
    if (!session || !link || !link.consumerReady) { status(link && !link.enabled ? "请先开启蓝牙" : "请先配对并连接"); return false; }
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
    const link=Ouse.bluetooth.get();
    if (session) {
        if(link && link.session===session){status(link.consumerReady ? "已连接" : !link.connected ? "请先配对并连接" : "等待输入服务");if(!link.consumerReady && playing)spin(false);return;}
        session=0;stage="open";spin(false);
    }
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
    if(!link || !link.enabled || !link.ready){status("请先开启蓝牙");return;}
    if(!link.connected){status("请先配对并连接");return;}
    const result = Ouse.bluetooth.open("hid");
    if (result) {
        session = result;
        status(link.consumerReady ? "已连接" : "等待输入服务");
    } else status(link.closing ? "正在释放连接" : link.busy ? "连接被其他应用占用" : "连接暂不可用");
}, 200);
Ouse.timers.setInterval(function () {
    if (!playing || !session) return;
    frame = (frame + 1) % frames.length;
    Ouse.ui.setImage("disc", frames[frame]);
}, 140);
spin(false);
arm();
globalThis.app = {
    onPause: function () { Ouse.bluetooth.close(); session = 0; playing = false; },
    onResume: function () { status("连接中"); arm(); },
    onClose: function () { Ouse.bluetooth.close(); session = 0; playing = false; }
};
