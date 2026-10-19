"use strict";
let page="album",offset=0,entries=[],pending=0,kind="",notice=false,returning=false,selected=0,scrollTarget=0,lastScroll=0;
let externalPath="";
function openExternal(){
 if(!externalPath||pending)return;
 if(!ready()){externalPath="";popup("SD 卡不可用，请检查连接。");return;}
 const path=externalPath;externalPath="";entries=[{path,video:/\.avi$/i.test(path)}];selected=0;open(0);
}
Ouse.files.onOpen(event=>{externalPath=event.path;if(pending){Ouse.tasks.cancel(pending);return;}openExternal();});
function show(id,value){if(value)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function popup(value){Ouse.ui.setText(page==="album"?"detail":"viewerDetail",value);Ouse.ui.show(page==="album"?"dialog":"viewerDialog");}
function ready(){const card=Ouse.files.get();return !!(card.ready&&card.present);}
function authorize(name,action){
 if(!Ouse.permissions){action();return;}
 const state=Ouse.permissions.request(name);
 if(state==="granted"){action();return;}if(state==="denied"){popup("权限未开启。");return;}
 let ticks=0;const timer=Ouse.timers.setInterval(()=>{const result=Ouse.permissions.get(name);if(result!=="ask"||++ticks>=150){Ouse.timers.clear(timer);if(result==="granted")action();else popup("权限未开启。");}},100);
}
function paint(){
 if(page!=="album")return;
 for(let index=0;index<12;++index){show("tile"+index,!!entries[index]);show("video"+index,!!(entries[index]&&entries[index].video));Ouse.ui.setEnabled("tile"+index,!!entries[index]&&!pending&&ready());}
 for(let row=0;row<4;++row)show("row"+row,!!entries[row*3]);
 show("empty",ready()&&!pending&&!entries.length);
}
function list(start,position=0){
 if(pending)return;
 start=Math.max(0,Math.min(4095,start));
 if(!ready()){paint();if(!notice){notice=true;popup("请插入 SD 卡后再打开相册。");}return;}
 notice=false;scrollTarget=position;pending=Ouse.photos.list(start);kind="list";if(!pending)popup("相册暂不可用，请重新进入。");paint();
}
function open(index){
 if(pending||!entries[index])return;if(!ready()){popup("请插入 SD 卡后再查看。");return;}
 selected=index;pending=Ouse.photos.open(entries[index].path);kind="open";
 if(!pending)popup("无法打开，请稍后重试。");paint();
}
function leave(){Ouse.photos.stop();Ouse.navigation.back();}
function bind(){
 if(page==="album"){
  Ouse.ui.on("dismiss","click",()=>Ouse.ui.hide("dialog"));
  for(let index=0;index<12;++index)Ouse.ui.on("tile"+index,"click",()=>open(index));paint();
 }else{
  Ouse.ui.on("back","click",leave);Ouse.ui.on("viewerTap","click",leave);
  Ouse.ui.on("viewerPrevious","click",()=>open(Math.max(0,selected-1)));
  Ouse.ui.on("viewerNext","click",()=>open(Math.min(entries.length-1,selected+1)));
  Ouse.ui.on("replay","click",()=>open(selected));
  Ouse.ui.on("viewerDismiss","click",()=>Ouse.ui.hide("viewerDialog"));
  Ouse.ui.on("delete","click",()=>{if(!pending&&entries[selected]){Ouse.photos.stop();Ouse.ui.show("deleteDialog");}});
  Ouse.ui.on("cancelDelete","click",()=>Ouse.ui.hide("deleteDialog"));
  Ouse.ui.on("confirmDelete","click",()=>{
   if(pending||!entries[selected])return;Ouse.ui.hide("deleteDialog");
   if(!ready()){popup("SD 卡不可用，请检查连接。");return;}
   const path=entries[selected].path;authorize("sharedDelete",()=>{if(pending||!ready())return;pending=Ouse.photos.remove(path);kind="remove";if(!pending)popup("删除失败，请稍后重试。");viewer();});
  });viewer();
 }
}
function dateText(entry){
 const match=entry&&/\/(?:IMG|VID)_(\d+)_/.exec(entry.path);
 const seconds=match?Number(match[1]):0;
 if(seconds<1577836800)return "拍摄时间未知";
 const date=new Date(seconds*1000),pad=value=>String(value).padStart(2,"0");
 return date.getFullYear()+"-"+pad(date.getMonth()+1)+"-"+pad(date.getDate())+" "+pad(date.getHours())+":"+pad(date.getMinutes());
}
function viewer(){
 if(page!=="photo")return;
 Ouse.ui.setText("takenAt",dateText(entries[selected]));
 Ouse.ui.setEnabled("viewerPrevious",!pending&&selected>0);Ouse.ui.setEnabled("viewerNext",!pending&&selected+1<entries.length);Ouse.ui.setEnabled("delete",!pending&&ready());
 show("replay",!!(entries[selected]&&entries[selected].video&&!Ouse.photos.get().playing&&!pending));
}
Ouse.navigation.onChange(event=>{page=event.page;bind();if(page==="album"){Ouse.photos.stop();if(returning){returning=false;list(offset,scrollTarget);}}});
Ouse.tasks.onComplete(event=>{
 if(event.id!==pending)return;pending=0;
 if(externalPath){openExternal();return;}
 if(event.state!=="succeeded"){popup(kind==="remove"?"删除失败，请检查 SD 卡。":"读取失败，请检查 SD 卡及文件格式。");paint();viewer();return;}
 const state=Ouse.photos.get();
 if(kind==="list"){entries=state.entries;offset=state.offset;paint();lastScroll=scrollTarget;Ouse.ui.scrollTo("grid",scrollTarget);}
 else if(kind==="remove"){returning=true;scrollTarget=0;offset=Math.max(0,offset-3);Ouse.navigation.back();}
 else if(page==="album"){returning=true;scrollTarget=Ouse.ui.getScroll("grid").y;Ouse.navigation.push("photo");}else viewer();
});
Ouse.timers.setInterval(()=>{
 if(!ready()){Ouse.photos.stop();if(pending)Ouse.tasks.cancel(pending);entries=[];paint();if(!notice){notice=true;popup("SD 卡不可用，请检查连接。");}}
 else if(notice){notice=false;if(page==="album"){Ouse.ui.hide("dialog");list(0);}}
 else if(page==="album"&&!pending){
  const scroll=Ouse.ui.getScroll("grid"),state=Ouse.photos.get();
  if(scroll.height&&!scroll.pending){
   const upward=scroll.y<lastScroll-2,downward=scroll.y>lastScroll+2;lastScroll=scroll.y;
   if(offset>0&&upward&&scroll.y<24)list(Math.max(0,offset-3),Math.max(0,scroll.y)+106);
   else if(state.more&&offset<4095&&downward&&scroll.bottom<60)list(offset+3,Math.max(0,scroll.y-106));
  }
 }
 viewer();
},300);
globalThis.app={onPause(){Ouse.photos.stop();},onClose(){Ouse.photos.stop();},onResume(){if(page==="album")list(offset,scrollTarget);else if(!ready())popup("请插入 SD 卡后再查看。");}};
bind();list(0);
