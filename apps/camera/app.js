"use strict";
let pending=0,kind="",opening=true,attempts=0,active=true;
let permissionAsked=false;
const labels=Object.create(null);
function text(id,value){if(labels[id]!==value){labels[id]=value;Ouse.ui.setText(id,value);}}
function show(id,value){if(value)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function popup(message){show("permissionSettings",Ouse.permissions.get("camera")==="denied");text("detail",message);Ouse.ui.show("dialog");}
function ready(){const card=Ouse.files.get();return !!(card.ready&&card.present);}
function thumbnail(){if(!ready()||pending)return;pending=Ouse.photos.list(0);kind="list";}
function start(){
 if(!opening||pending||!active||Ouse.power.get().asleep)return;
 if(Ouse.permissions){
  const permission=permissionAsked?Ouse.permissions.get("camera"):Ouse.permissions.request("camera");permissionAsked=true;
  if(permission!=="granted"){if(permission==="denied"){opening=false;popup("摄像头权限已拒绝，可前往权限设置开启。");}return;}
 }
 const state=Ouse.camera.get();if(!state||state.busy)return;
 if(state.running){opening=false;return;}
 if(state.recoveryPending){opening=false;popup("摄像头释放失败，请重启设备。");return;}
 pending=Ouse.camera.start();kind="start";
 if(pending)opening=false;
 else if(++attempts>=10){opening=false;popup(state.inUse?"摄像头正被其他应用使用。":"摄像头无法打开，请重新进入相机。");}
}
function refresh(){
 const previous=Ouse.camera.get();
 if(active&&!pending&&!opening&&previous&&!previous.running&&!previous.busy&&!previous.inUse&&!previous.recoveryPending&&previous.error==="ESP_OK"&&Ouse.permissions.get("camera")==="granted"&&!Ouse.power.get().asleep){opening=true;attempts=0;}
 start();const state=Ouse.camera.get();if(!state)return;
 Ouse.ui.setEnabled("shutter",!opening&&!pending&&!state.busy&&state.running);
 Ouse.ui.setEnabled("album",!opening&&!pending&&!state.busy);
}
Ouse.ui.on("album","click",()=>{if(pending)return;if(!ready()){popup("请插入 SD 卡后再打开相册。");return;}Ouse.openApp("user.gallery");});
Ouse.ui.on("permissionSettings","click",()=>{Ouse.ui.hide("dialog");Ouse.system.openSettings("permissions");});
Ouse.ui.on("dismiss","click",()=>Ouse.ui.hide("dialog"));
function capture(){
 if(pending||opening)return;
 const state=Ouse.camera.get();
 if(!ready()){popup("请插入 SD 卡后再拍摄。");return;}
 if(!state.running){popup("摄像头无法打开，请重新进入相机。");return;}
 kind="capture";pending=Ouse.camera.capture();
 if(!pending)popup("操作失败，请稍后重试。");refresh();
}
Ouse.ui.on("shutter","click",capture);
Ouse.keys.on("pwr",capture);
Ouse.tasks.onComplete(event=>{
 if(event.id!==pending)return;pending=0;
 if(event.state!=="succeeded")popup(kind==="start"?"摄像头无法打开，请检查连接后重新进入相机。":kind==="list"?"无法读取相册。":"拍摄失败，请检查 SD 卡和剩余空间。");
 else if(kind==="list")show("albumThumb",!!Ouse.photos.get().entries.length);
 else if(kind==="start"||kind==="capture")thumbnail();
 refresh();
});
Ouse.timers.setInterval(refresh,200);
globalThis.app={onPause(){active=false;opening=false;Ouse.camera.stop();},onResume(){active=true;pending=0;opening=true;attempts=0;refresh();},onClose(){active=false;Ouse.camera.stop();}};
refresh();
