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
let token = 0;
let step = 24;
let held = 0,travel = 0;
let lastStatus="";
function status(text) { if(lastStatus!==text){lastStatus=text;Ouse.ui.setText("status", text);} }
function move(x, y, wheel) {
    if (!linkGuide.poll()) return false;
    return !!Ouse.bluetooth.mouse(held, x, y, wheel || 0);
}
function click(button) {
    if (!linkGuide.poll()) return;
    if (!Ouse.bluetooth.mouse(button, 0, 0, 0)) return;
    const mine = ++token;
    Ouse.timers.setTimeout(function () {
        if (mine === token) Ouse.bluetooth.mouse(0, 0, 0, 0);
    }, 100);
}
const linkGuide=createLinkGuide("mouseReady",()=>{held=0;++token;});
function arm(){session=0;linkGuide.arm();}
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
Ouse.timers.setInterval(function () {const ready=linkGuide.poll();if(held&&ready)Ouse.bluetooth.mouse(held,0,0,0);},200);
arm();
globalThis.app = {
    onPause: function () { held=0;Ouse.bluetooth.close(); session = 0; },
    onResume: function () { status("连接中"); arm(); },
    onClose: function () { held=0;Ouse.bluetooth.close(); session = 0; }
};
