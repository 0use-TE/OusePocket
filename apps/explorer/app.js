"use strict";
let directory="/",start=0,requestId=0,entries=[],more=false,waiting=false,lastReady=null,notice="",requestedPath="/",requestedOffset=0,kind="reset",settle=0,failed=false;
const labels=Object.create(null),icons=Object.create(null);
let installId=0,installPhase="";
function short(value,limit=230){let text="",bytes=0;for(const char of String(value)){const point=char.codePointAt(0),size=point<128?1:point<2048?2:point<65536?3:4;if(bytes+size>limit)return text+"…";text+=char;bytes+=size;}return text;}
function text(id,value){value=short(value);if(labels[id]!==value){labels[id]=value;Ouse.ui.setText(id,value);}}
function size(bytes){if(bytes<1024)return bytes+" B";if(bytes<1024*1024)return (bytes/1024).toFixed(1)+" KiB";if(bytes<1024*1024*1024)return (bytes/1024/1024).toFixed(1)+" MiB";return (bytes/1024/1024/1024).toFixed(2)+" GiB";}
function popup(title,body){text("dialog",title);text("detail",body);Ouse.ui.show("dialog");}
function status(value){text("status",value);if(value)Ouse.ui.show("status");else Ouse.ui.hide("status");}
function controls(){
 const ready=Ouse.files.get().ready;
 Ouse.navigation.setBackEnabled(ready&&(waiting||directory!=="/"));
 Ouse.ui.setEnabled("up",ready&&!waiting&&directory!=="/");Ouse.ui.setEnabled("refresh",ready&&!waiting);Ouse.ui.setEnabled("info",ready);
 for(let index=0;index<24;++index){const item=entries[index];if(item){
  Ouse.ui.show("row"+index);text("entry"+index,item.name);Ouse.ui.setEnabled("entry"+index,ready&&!waiting);
  const icon=item.directory?"folder":"document";if(icons[index]!==icon){icons[index]=icon;Ouse.ui.setImage("icon"+index,icon);}
 }else Ouse.ui.hide("row"+index);}
 text("title",directory==="/"?"SD 卡":short(directory.slice(directory.lastIndexOf("/")+1),42));
}
function browse(path,skip=0,action="reset"){
 if(waiting)return;if(!Ouse.files.get().ready){poll();return;}
 let id=false;try{id=Ouse.files.browse(path,skip);}catch(error){}
 if(!id){failed=true;popup("读取失败","SD 卡不可用或文件服务繁忙，请稍后刷新。");return;}
 requestedPath=path;requestedOffset=skip;kind=action;requestId=id;waiting=true;failed=false;if(!entries.length)status("正在读取…");controls();
}
function poll(){
 if(installId){const state=Ouse.packages.state();if(state&&state.taskId===installId){if(installPhase!==state.phase){installPhase=state.phase;status(({queued:"等待安装",downloading:"正在读取安装包",confirmation:"请确认系统安装提示",installing:"正在安装",succeeded:"安装完成",cancelled:"已取消安装",failed:"安装失败："+state.error})[state.phase]||"");}if(!state.busy)installId=0;}else installId=0;}
 const card=Ouse.files.get();
 if(!card.ready){
  if(requestId)Ouse.tasks.cancel(requestId);requestId=0;waiting=false;entries=[];more=false;start=0;controls();
  const reason=card.detected?"无法读取 SD 卡":"未插入 SD 卡";status(reason);
  if(notice!==reason){notice=reason;popup(reason,card.detected?"请检查 SD 卡文件系统或连接状态。不会自动格式化。":"请插入 SD 卡后重启设备。");}
  lastReady=false;return;
 }
 if(lastReady!==true){lastReady=true;notice="";Ouse.ui.hide("dialog");browse(directory);return;}
 if(requestId){
  const result=Ouse.files.directory(requestId);if(!result || result.pending)return;requestId=0;waiting=false;
  if(result.error!=="ESP_OK"){failed=true;status(entries.length?"":"读取失败");popup("读取失败",result.error);controls();return;}
  const scroll=Ouse.ui.getScroll("entries");
  if(kind==="append"){
   entries=entries.concat(result.entries);more=result.more;
   if(entries.length>24){entries.splice(0,8);start+=8;Ouse.ui.scrollTo("entries",Math.max(0,scroll.y-8*56));}
  }else if(kind==="prepend"){
   entries=result.entries.concat(entries);start=requestedOffset;more=entries.length>24||more;entries=entries.slice(0,24);Ouse.ui.scrollTo("entries",Math.max(0,scroll.y)+result.entries.length*56);
  }else{
   directory=requestedPath;start=requestedOffset;entries=result.entries;more=result.more;Ouse.ui.scrollTo("entries",0);
  }
  settle=2;status(entries.length?"":"此文件夹为空");controls();return;
 }
 if(settle){--settle;return;}if(waiting||failed)return;
 const scroll=Ouse.ui.getScroll("entries");if(!scroll.height||scroll.pending)return;
 if(start>0&&scroll.y<56)browse(directory,Math.max(0,start-8),"prepend");
 else if(more&&scroll.bottom<112)browse(directory,start+entries.length,"append");
}
function up(){if(waiting)return;const split=directory.lastIndexOf("/");browse(split>0?directory.slice(0,split):"/");}
Ouse.navigation.onBack(up);
Ouse.ui.on("up","click",up);
Ouse.ui.on("refresh","click",()=>browse(directory));
Ouse.ui.on("info","click",()=>{const card=Ouse.files.get();popup("存储信息","SD 卡\n可用："+size(card.freeBytes)+"\n总计："+size(card.totalBytes));});
Ouse.ui.on("dismiss","click",()=>Ouse.ui.hide("dialog"));
for(let index=0;index<24;++index)Ouse.ui.on("entry"+index,"click",()=>{
 const item=entries[index];if(!item||waiting)return;
 if(!item.accessible){popup("无法访问","文件信息不可用或路径过长。");return;}
 if(item.directory){browse(item.path);return;}
 if(/\.bpk$/i.test(item.name)){if(installId){popup("请稍候","另一个安装正在进行。");return;}const id=Ouse.packages.installFromSD(item.path);if(!id){popup("无法安装","安装服务繁忙或 SD 卡不可用。");return;}installId=id;installPhase="";Ouse.ui.hide("dialog");return;}
 if(/\.(jpe?g|wav|avi)$/i.test(item.name)){if(!Ouse.files.open(item.path))popup("无法打开","文件路径过长或打开服务不可用。");return;}
 popup("文件详情",short(item.name,100)+"\n大小："+size(item.bytes)+"\n路径："+short(item.path,100));
});
Ouse.timers.setInterval(poll,150);
globalThis.app={onResume(){notice="";poll();},onClose(){if(requestId)Ouse.tasks.cancel(requestId);if(installId)Ouse.tasks.cancel(installId);}};
controls();poll();
