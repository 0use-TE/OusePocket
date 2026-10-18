"use strict";
let stage = "";
let session = 0;
let token = 0;
function status(text) { Ouse.ui.setText("status", text); }
function key(mod, keys) {
    if (!session) { status("未连接"); return false; }
    if (!Ouse.bluetooth.keyboard(mod, keys)) return false;
    const mine = ++token;
    Ouse.timers.setTimeout(function () {
        if (mine === token) Ouse.bluetooth.release();
    }, 100);
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
Ouse.ui.on("prev", "click", function () { key(0, [75]); });
Ouse.ui.on("next", "click", function () { key(0, [78]); });
Ouse.ui.on("start", "click", function () { key(0, [62]); });
Ouse.ui.on("end", "click", function () { key(0, [41]); });
Ouse.ui.on("black", "click", function () { key(0, [5]); });
Ouse.ui.on("left", "click", function () { key(0, [80]); });
Ouse.ui.on("right", "click", function () { key(0, [79]); });
Ouse.ui.on("up", "click", function () { key(0, [82]); });
Ouse.ui.on("down", "click", function () { key(0, [81]); });
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
    if (result) { session = result; status("已连接"); }
    else status("连接占用");
}, 200);
arm();
globalThis.app = {
    onPause: function () { Ouse.bluetooth.close(); session = 0; },
    onResume: function () { status("连接中"); arm(); },
    onClose: function () { Ouse.bluetooth.close(); session = 0; }
};
