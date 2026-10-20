"use strict";
// Shared Bluetooth setup flow. Source: tools/app-shared/bluetooth-guide.js.
function createLinkGuide(kind,onLost){
 let asked="",wasReady=false,caption="",shown=null;
 function button(value,visible){if(caption!==value){caption=value;Ouse.ui.setText("connect",value);}if(shown!==visible){shown=visible;visible?Ouse.ui.show("connect"):Ouse.ui.hide("connect");}}
 function missing(){for(const name of ["bluetooth","hid"]){const state=Ouse.permissions.get(name);if(state!=="granted")return {name,state};}return null;}
 function unavailable(message,action){if(wasReady&&onLost)onLost();wasReady=false;status(message);button(action,true);return false;}
 function poll(){
  const permission=missing();
  if(permission){session=0;if(permission.state==="ask"&&asked!==permission.name){asked=permission.name;Ouse.permissions.request(permission.name);}return unavailable(permission.state==="denied"?"权限未开启":"等待授权",permission.state==="denied"?"权限设置":"授权");}
  const link=Ouse.bluetooth.get();
  if(session&&(!link||link.session!==session)){session=0;if(onLost)onLost();}
  if(!link||!link.enabled)return unavailable("蓝牙已关闭","开启蓝牙");
  if(!link.ready)return unavailable("蓝牙启动中","蓝牙设置");
  if(!link.connected)return unavailable("尚未连接设备","连接蓝牙");
  if(!session)session=Ouse.bluetooth.open("hid")||0;
  if(!session)return unavailable(link.closing?"连接释放中":link.busy?"连接被占用":"连接暂不可用","重试连接");
  if(!link[kind])return unavailable("等待输入服务","蓝牙设置");
  wasReady=true;status("已连接");button("连接蓝牙",false);return true;
 }
 function action(){const permission=missing();if(permission){if(permission.state==="ask"){asked=permission.name;Ouse.permissions.request(permission.name);poll();}else Ouse.system.openSettings("permissions");return;}const link=Ouse.bluetooth.get();if(!link||!link.enabled||!link.ready||!link.connected||session)Ouse.system.openSettings("bluetooth");else poll();}
 Ouse.ui.on("connect","click",action);
 return {poll,arm(){asked="";poll();}};
}

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
let lastStatus="";
function status(text) { if(lastStatus!==text){lastStatus=text;Ouse.ui.setText("status", text);} }
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
    if (!linkGuide.poll()) return;
    play([{ mod: mod, keys: keys, hold: 120, gap: 100 }]);
}
function runCommand(command) {
    if (!linkGuide.poll()) return;
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
const linkGuide=createLinkGuide("keyboardReady",()=>{++seq;busy=false;});
function arm(){session=0;linkGuide.arm();}
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
Ouse.timers.setInterval(function () {const ready=linkGuide.poll();},200);
arm();
globalThis.app = {
    onPause: function () { seq += 1; Ouse.bluetooth.close(); session = 0; busy = false; },
    onResume: function () { status("连接中"); arm(); },
    onClose: function () { seq += 1; Ouse.bluetooth.close(); session = 0; busy = false; }
};
