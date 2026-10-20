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
let lastStatus="";
function status(text) { if(lastStatus!==text){lastStatus=text;Ouse.ui.setText("status", text);} }
function key(mod, keys) {
    if (!linkGuide.poll()) return false;
    if (!Ouse.bluetooth.keyboard(mod, keys)) return false;
    const mine = ++token;
    Ouse.timers.setTimeout(function () {
        if (mine === token) Ouse.bluetooth.release();
    }, 100);
    return true;
}
const linkGuide=createLinkGuide("keyboardReady",()=>{++token;});
function arm(){session=0;linkGuide.arm();}
Ouse.ui.on("prev", "click", function () { key(0, [75]); });
Ouse.ui.on("next", "click", function () { key(0, [78]); });
Ouse.ui.on("start", "click", function () { key(0, [62]); });
Ouse.ui.on("end", "click", function () { key(0, [41]); });
Ouse.ui.on("black", "click", function () { key(0, [5]); });
Ouse.ui.on("left", "click", function () { key(0, [80]); });
Ouse.ui.on("right", "click", function () { key(0, [79]); });
Ouse.ui.on("up", "click", function () { key(0, [82]); });
Ouse.ui.on("down", "click", function () { key(0, [81]); });
Ouse.timers.setInterval(function () {const ready=linkGuide.poll();},200);
arm();
globalThis.app = {
    onPause: function () { Ouse.bluetooth.close(); session = 0; },
    onResume: function () { status("连接中"); arm(); },
    onClose: function () { Ouse.bluetooth.close(); session = 0; }
};
function resizePresenter(){
 const d=Ouse.display.get(),w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),land=w>h,x=land?176:12,width=land?w-188:w-24;
 Ouse.ui.layout([
  {id:'title',x:12,y:8,width:160,height:32},{id:'status',x:w-124,y:8,width:112,height:32},
  {id:'stage',x:12,y:48,width:land?148:w-24,height:88},
  {id:'hint',x:0,y:28,width:land?148:w-24,height:32},
  {id:'connect',x:12,y:land?h-56:148,width:land?148:w-24,height:36},
  {id:'pages',x,y:land?52:h-200,width,height:64},
  {id:'show',x,y:land?128:h-124,width,height:48},
  {id:'arrows',x,y:land?188:h-64,width,height:48}
 ]);
}
Ouse.display.onChange(resizePresenter);
const presenterResume=globalThis.app.onResume;
globalThis.app.onResume=function(){resizePresenter();presenterResume();};
resizePresenter();
