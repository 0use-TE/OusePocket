"use strict";
let stage = "";
let session = 0;
let token = 0;
let step = 24;
let held = 0,travel = 0;
function status(text) { Ouse.ui.setText("status", text); }
function move(x, y, wheel) {
    if (!session) { status("未连接"); return false; }
    return !!Ouse.bluetooth.mouse(held, x, y, wheel || 0);
}
function click(button) {
    if (!session) { status("未连接"); return; }
    if (!Ouse.bluetooth.mouse(button, 0, 0, 0)) return;
    const mine = ++token;
    Ouse.timers.setTimeout(function () {
        if (mine === token) Ouse.bluetooth.mouse(0, 0, 0, 0);
    }, 100);
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
Ouse.ui.on("pad", "pointerdown", function () { travel=0; });
Ouse.ui.on("pad", "pointermove", function (event) {
    travel+=Math.abs(event.dx)+Math.abs(event.dy);
    move(Math.max(-127,Math.min(127,Math.round(event.dx*step/24))),Math.max(-127,Math.min(127,Math.round(event.dy*step/24))),0);
});
Ouse.ui.on("pad", "pointerup", function () { if(travel<6)click(1); });
function release(){held=0;Ouse.bluetooth.release();}
Ouse.ui.on("pad", "pointercancel", release);
for(const [id,button] of [["left",1],["right",2]]){
    Ouse.ui.on(id,"pointerdown",function(){const link=Ouse.bluetooth.get();if(session&&link&&link.mouseReady){held=button;Ouse.bluetooth.mouse(held,0,0,0);}});
    Ouse.ui.on(id,"pointerup",release);Ouse.ui.on(id,"pointercancel",release);
    Ouse.ui.on(id,"pointermove",function(event){move(Math.max(-127,Math.min(127,event.dx)),Math.max(-127,Math.min(127,event.dy)),0);});
}
Ouse.ui.on("up", "click", function () { move(0, 0, 3); });
Ouse.ui.on("down", "click", function () { move(0, 0, -3); });
Ouse.ui.on("fast", "click", function () {
    step = step === 24 ? 48 : 24;
    Ouse.ui.setText("fast", step === 48 ? "慢速" : "快速");
});
Ouse.timers.setInterval(function () {
    const link=Ouse.bluetooth.get();
    if(session && link && link.session===session){status(link.mouseReady ? "已连接" : !link.connected ? "请先配对并连接" : "等待输入服务");if(!link.mouseReady)held=0;else if(held)Ouse.bluetooth.mouse(held,0,0,0);return;}
    if(session){session=0;held=0;stage="open";}
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
    if (result) { session = result; status("等待输入服务"); }
    else status(link.closing ? "正在释放连接" : link.busy ? "连接被其他应用占用" : "连接暂不可用");
}, 200);
arm();
globalThis.app = {
    onPause: function () { held=0;Ouse.bluetooth.close(); session = 0; },
    onResume: function () { status("连接中"); arm(); },
    onClose: function () { held=0;Ouse.bluetooth.close(); session = 0; }
};
