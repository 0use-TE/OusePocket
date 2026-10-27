"use strict";
let pending=0,kind="",opening=true,attempts=0,active=true;
let permissionAsked=false;
let fillPreview=true;
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
Ouse.ui.on("previewMode","click",()=>{fillPreview=!fillPreview;layout();});
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
 if(event.state!=="succeeded"){
  const stage=Ouse.camera.get().errorStage;
  popup(kind==="start"?"摄像头无法打开，请检查连接后重新进入相机。":kind==="list"?"无法读取相册。":stage==="storage"?"照片保存失败，请检查 SD 卡和剩余空间。":stage==="encode"?"照片编码失败，请稍后重试。":"摄像头暂未就绪，请稍后再拍。");
 }
 else if(kind==="list"){const entries=Ouse.photos.get().entries;if(entries.length)Ouse.ui.setImage("albumThumb",entries[0].image);show("albumThumb",!!entries.length);}
 else if(kind==="start"||kind==="capture")thumbnail();
 refresh();
});
Ouse.timers.setInterval(refresh,200);
function layout(){
 if(!active)return;
 const display=Ouse.display.get(),w=Math.floor(display.contentWidthDp),h=Math.floor(display.contentHeightDp),land=w>h;
 const rail=96;
 // @camera is a 320x240 image. Keep its 4:3 ratio and let viewfinder clip
 // the centered overflow. Whole 4x3 units avoid orientation-dependent stretch.
 const unit=fillPreview?Math.ceil(Math.max(w/4,h/3))+1:Math.max(1,Math.floor(Math.min(w/4,h/3)));
 const pw=unit*4,ph=unit*3;
 text("previewMode",fillPreview?"完整画面":"铺满取景");
 Ouse.ui.layout([
  {id:"viewfinder",x:0,y:0,width:w,height:h},{id:"preview",x:Math.floor((w-pw)/2),y:Math.floor((h-ph)/2),width:pw,height:ph},
  {id:"previewMode",x:12,y:12,width:100,height:36},
  {id:"controls",x:land?w-rail:0,y:land?0:h-rail,width:land?rail:w,height:land?h:rail},
  {id:"shutter",x:land?10:Math.floor((w-76)/2),y:land?Math.floor((h-76)/2):10,width:76,height:76},
  {id:"album",x:20,y:land?Math.max(12,Math.floor(h/2)-116):20,width:56,height:56}
 ]);
}
Ouse.display.onChange(layout);
globalThis.app={onPause(){active=false;opening=false;Ouse.camera.stop();},onResume(){active=true;pending=0;opening=true;attempts=0;Ouse.display.setFullscreen(true);layout();refresh();},onClose(){active=false;Ouse.camera.stop();}};
Ouse.display.setFullscreen(true);
layout();
refresh();
