"use strict";
let stage = "";
let session = 0;
let token = 0;
let step = 24;
function status(text) { Ouse.ui.setText("status", text); }
function move(x, y, wheel) {
    if (!session) { status("未连接"); return false; }
    return !!Ouse.bluetooth.mouse(0, x, y, wheel || 0);
}
function click(button) {
    if (!session) { status("未连接"); return; }
    if (!Ouse.bluetooth.mouse(button, 0, 0, 0)) return;
    const mine = ++token;
    Ouse.timers.setTimeout(function () {
        if (mine === token) Ouse.bluetooth.mouse(0, 0, 0, 0);
    }, 100);
}
function burst(dx, dy) {
    if (!session) { status("未连接"); return; }
    for (let n = 0; n < 4; n++) move(dx, dy, 0);
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
Ouse.ui.on("pad", "swipe", function (dir) {
    if (dir === "left") burst(-step, 0);
    else if (dir === "right") burst(step, 0);
    else if (dir === "up") burst(0, -step);
    else if (dir === "down") burst(0, step);
});
Ouse.ui.on("pad", "click", function () { click(1); });
Ouse.ui.on("left", "click", function () { click(1); });
Ouse.ui.on("right", "click", function () { click(2); });
Ouse.ui.on("up", "click", function () { move(0, 0, 3); });
Ouse.ui.on("down", "click", function () { move(0, 0, -3); });
Ouse.ui.on("fast", "click", function () {
    step = step === 24 ? 48 : 24;
    Ouse.ui.setText("fast", step === 48 ? "慢速" : "快速");
});
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
