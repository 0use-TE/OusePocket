"use strict";
let stage = "";
let session = 0;
let busy = false;
let seq = 0;
const KEYS = {
    a: 0x04, b: 0x05, c: 0x06, d: 0x07, e: 0x08, f: 0x09, g: 0x0A, h: 0x0B,
    i: 0x0C, j: 0x0D, k: 0x0E, l: 0x0F, m: 0x10, n: 0x11, o: 0x12, p: 0x13,
    q: 0x14, r: 0x15, s: 0x16, t: 0x17, u: 0x18, v: 0x19, w: 0x1A, x: 0x1B,
    y: 0x1C, z: 0x1D, ".": 0x37
};
function status(text) { Ouse.ui.setText("status", text); }
function codesFor(text) {
    const out = [];
    for (let i = 0; i < text.length; i++) {
        const code = KEYS[text.charAt(i)];
        if (!code) return null;
        out.push(code);
    }
    return out;
}
function play(steps) {
    const mine = ++seq;
    let index = 0;
    busy = true;
    function finish() { if (mine === seq) busy = false; }
    function next() {
        if (mine !== seq || !session) { finish(); return; }
        if (index >= steps.length) { finish(); return; }
        const step = steps[index++];
        if (!Ouse.bluetooth.keyboard(step.mod || 0, step.keys)) { finish(); return; }
        Ouse.timers.setTimeout(function () {
            if (mine !== seq) return;
            Ouse.bluetooth.release();
            Ouse.timers.setTimeout(next, step.gap || 120);
        }, step.hold || 120);
    }
    next();
}
function chord(mod, keys) {
    if (!session) { status("未连接"); return; }
    play([{ mod: mod, keys: keys, hold: 120, gap: 100 }]);
}
function runCommand(command) {
    if (!session) { status("未连接"); return; }
    if (busy) return;
    const codes = codesFor(command);
    if (!codes) return;
    // 不盲切中英文。确认用小键盘回车，中文输入法通常不会当成上屏键。
    const steps = [{ mod: 0x08, keys: [0x15], hold: 140, gap: 400 }];
    for (let i = 0; i < codes.length; i++) {
        steps.push({ mod: 0, keys: [codes[i]], hold: 120, gap: 120 });
    }
    steps.push({ mod: 0, keys: [0x58], hold: 180, gap: 200 });
    play(steps);
}
function arm() {
    session = 0;
    busy = false;
    seq += 1;
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
Ouse.ui.on("term", "click", function () { runCommand("wt"); });
Ouse.ui.on("cmd", "click", function () { runCommand("cmd"); });
Ouse.ui.on("ps", "click", function () { runCommand("powershell"); });
Ouse.ui.on("tab", "click", function () { chord(0x01, [0x2B]); });
Ouse.ui.on("newTab", "click", function () { chord(0x01, [0x17]); });
Ouse.ui.on("closeTab", "click", function () { chord(0x01, [0x1A]); });
Ouse.ui.on("window", "click", function () { chord(0x04, [0x2B]); });
Ouse.ui.on("desktop", "click", function () { chord(0x08, [0x07]); });
Ouse.ui.on("run", "click", function () { chord(0x08, [0x15]); });
Ouse.ui.on("taskmgr", "click", function () { chord(0x03, [0x29]); });
Ouse.ui.on("explorer", "click", function () { chord(0x08, [0x08]); });
Ouse.ui.on("desktops", "click", function () { chord(0x08, [0x2B]); });
Ouse.ui.on("services", "click", function () { runCommand("services.msc"); });
Ouse.ui.on("firewall", "click", function () { runCommand("firewall.cpl"); });
Ouse.ui.on("control", "click", function () { runCommand("control"); });
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
    onPause: function () { seq += 1; Ouse.bluetooth.close(); session = 0; busy = false; },
    onResume: function () { status("连接中"); arm(); },
    onClose: function () { seq += 1; Ouse.bluetooth.close(); session = 0; busy = false; }
};
