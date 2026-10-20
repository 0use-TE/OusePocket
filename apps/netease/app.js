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
let playing = false;
let frame = 0;
const frames = ["disc0", "disc1", "disc2", "disc3"];
let lastStatus="";
function layout(){
 const d=Ouse.display.get(),w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),land=w>h;
 const x=land?160:12,width=land?w-172:w-24;
 Ouse.ui.layout([
  {id:'title',x:12,y:8,width:160,height:32},
  {id:'status',x:w-124,y:8,width:112,height:32},
  {id:'disc',x:land?12:Math.floor((w-128)/2),y:land?64:52,width:128,height:128},
  {id:'connect',x,y:land?h-48:196,width,height:40},
  {id:'transport',x,y:land?64:h-132,width,height:56},
  {id:'volume',x,y:land?132:h-64,width,height:48}
 ]);
}
Ouse.display.onChange(layout);
function status(text) { if(lastStatus!==text){lastStatus=text;Ouse.ui.setText("status", text);} }
function spin(on) {
    playing = !!on;
}
function tap(usage) {
    if(!linkGuide.poll())return false;
    const ok = Ouse.bluetooth.consumer(usage);
    if (!ok) return false;
    const mine = ++token;
    Ouse.timers.setTimeout(function () { if (mine === token) Ouse.bluetooth.release(); }, 100);
    return true;
}
const linkGuide=createLinkGuide("consumerReady",()=>{spin(false);++token;});
function arm(){session=0;linkGuide.arm();}
Ouse.ui.on("previous", "click", function () { if (tap(182)) spin(true); });
Ouse.ui.on("toggle", "click", function () { if (tap(205)) spin(!playing); });
Ouse.ui.on("next", "click", function () { if (tap(181)) spin(true); });
Ouse.ui.on("mute", "click", function () { tap(226); });
Ouse.ui.on("quieter", "click", function () { tap(234); });
Ouse.ui.on("louder", "click", function () { tap(233); });
Ouse.timers.setInterval(function () {const ready=linkGuide.poll();},200);
Ouse.timers.setInterval(function () {
    if (!playing || !session) return;
    frame = (frame + 1) % frames.length;
    Ouse.ui.setImage("disc", frames[frame]);
}, 140);
spin(false);
layout();
arm();
globalThis.app = {
    onPause: function () { Ouse.bluetooth.close(); session = 0; playing = false; },
    onResume: function () { layout();status("连接中"); arm(); },
    onClose: function () { Ouse.bluetooth.close(); session = 0; playing = false; }
};
