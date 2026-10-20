"use strict";
const ROWS=24,STEP=8,ROW_H=56;
let tracks=[],remote=[],selected=0,mode=0,page="player",loaded=false,libraryTab="sd";
let wanted=false,pending=-1,taskId=0,handled=0,saving=false,dirty=false,downloadId=0;
let cache=Object.create(null),libraryRevision=-1,cardReady=false,lastResult="";
let listStart=0,filterQuery="",settle=0;
let externalPath="";
function resizePlayer(){
 if(page!=="player")return;
 const d=Ouse.display.get(),w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),land=w>h,x=land?160:12,width=land?w-172:w-24;
 Ouse.ui.layout([
  {id:'title',x:12,y:8,width:100,height:28},{id:'library',x:w-76,y:8,width:64,height:32},
  {id:'record',x:land?12:Math.floor((w-128)/2),y:land?56:44,width:128,height:128},
  {id:'track',x,y:land?48:184,width,height:28},{id:'state',x,y:land?80:216,width,height:20},
  {id:'progress',x:x+8,y:land?108:244,width:width-16,height:5},{id:'time',x,y:land?120:254,width,height:18},
  {id:'transport',x:land?x:Math.floor((w-208)/2),y:land?144:h-124,width:208,height:56},
  {id:'actions',x,y:h-58+(land?24:0),width,height:30}
 ]);
}
Ouse.display.onChange(resizePlayer);
function openExternal(){
 if(!externalPath||!loaded)return;
 const path=externalPath;externalPath="";
 if(!filesGet().ready){lastResult="SD 卡不可用";refresh();return;}
 syncLibrary();let index=tracks.findIndex(track=>track.path===path);
 if(index<0){tracks.push({path,name:path.slice(path.lastIndexOf("/")+1)});index=tracks.length-1;}
 play(index);if(page!=="player")Ouse.navigation.back();
}
Ouse.files.onOpen(event=>{externalPath=event.path;openExternal();});
const titles={"fur-elise-30s.wav":"致爱丽丝 · 30 秒","mozart-allegro.wav":"莫扎特 · K545 快板","mozart-andante.wav":"莫扎特 · K545 行板","mozart-rondo.wav":"莫扎特 · K545 回旋曲"};
function style(id,patch){if(Ouse.ui.setStyle)Ouse.ui.setStyle(id,patch);}
function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function text(id,value){value=String(value);if(cache[id]!==value){cache[id]=value;Ouse.ui.setText(id,value);}}
function fmt(ms){const s=Math.floor(Math.max(0,ms)/1000);return Math.floor(s/60)+":"+(s%60<10?"0":"")+(s%60);}
function persist(){
 dirty=true;if(saving)return;saving=true;dirty=false;
 Ouse.storage.set("library",{version:2,tracks:remote,selectedPath:tracks[selected]?(tracks[selected].path||tracks[selected].url):"",mode}).then(()=>{saving=false;if(dirty)persist();},()=>{saving=false;dirty=false;});
}
function filesGet(){return Ouse.files?Ouse.files.get():{revision:0,ready:false,count:0,downloadTaskId:0,downloadBytes:0};}
function syncLibrary(){
 if(!Ouse.files){
  if(!tracks.some(track=>track.path))tracks=[{name:"致爱丽丝 · 30 秒",path:"Music/fur-elise-30s.wav"},{name:"莫扎特 · K545 快板",path:"Music/mozart-allegro.wav"}].concat(remote);
  return false;
 }
 const sd=Ouse.files.get();if(sd.revision===libraryRevision&&sd.ready===cardReady)return false;
 const current=tracks[selected],key=current&&(current.path||current.url);libraryRevision=sd.revision;cardReady=sd.ready;
 const files=[];if(sd.ready)for(let offset=0;offset<Math.min(sd.count,64);offset+=8)for(const item of Ouse.files.list(offset,8)){
  const file=item.path.slice(item.path.lastIndexOf("/")+1);files.push({name:titles[file]||file.replace(/\.wav$/i,""),path:item.path});
 }
 tracks=files.concat(remote);selected=Math.max(0,tracks.findIndex(track=>(track.path||track.url)===key));
 listStart=Math.min(listStart,Math.max(0,Math.ceil(Math.max(tracks.length-ROWS,0)/STEP)*STEP));return true;
}
function endLease(){wanted=false;Ouse.background.end();}
function lease(){wanted=Ouse.background.begin("music-playlist");}
function play(index){
 if(!loaded||index<0||index>=tracks.length)return;
 const media=Ouse.media.get();
 if(media&&media.active){pending=index;endLease();if(!Ouse.media.stop())pending=-1;return;}
 pending=-1;selected=index;handled=0;
 try{taskId=Ouse.media.play(tracks[index].path?{path:tracks[index].path}:{url:tracks[index].url});}catch(error){lastResult=error.message;return;}
 if(taskId&&Ouse.media.setInfo)Ouse.media.setInfo({title:tracks[index].name||"音乐",artist:tracks[index].artist||""});
 if(!taskId){endLease();lastResult="无法播放，请检查 SD 卡和 WAV 格式";return;}
 lease();persist();refresh();
}
function status(media){
 if(!tracks.length)return "请插入 SD 卡并打开曲库";
 if(!media)return lastResult||"准备就绪";
 if(media.errorCode)return "播放失败："+media.error;
 if(!media.active)return "已停止";
 if(media.interrupted)return "闹铃优先，等待恢复";
 if(media.paused)return "已暂停";
 return media.buffering?"正在缓冲":"正在播放";
}
function matchList(){
 const q=filterQuery.trim().toLowerCase(),out=[];
 for(let i=0;i<tracks.length;i++){
  const name=String(tracks[i].name||"");
  if(!q||name.toLowerCase().indexOf(q)>=0)out.push(i);
 }
 return out;
}
function paintTabs(){
 const sd=libraryTab==="sd";
 visible("filter",sd);visible("listStatus",sd);visible("sdList",sd);visible("netPanel",!sd);visible("rescan",sd);
 style("tabSd",sd?{backgroundColor:"$accent",textColor:"#ffffff",borderWidth:0}:{backgroundColor:"$surface",textColor:"$text",borderColor:"$border",borderWidth:1});
 style("tabNet",sd?{backgroundColor:"$surface",textColor:"$text",borderColor:"$border",borderWidth:1}:{backgroundColor:"$accent",textColor:"#ffffff",borderWidth:0});
}
function paintList(){
 const list=matchList();
 const maxStart=Math.max(0,list.length-ROWS);
 if(listStart>maxStart)listStart=maxStart-(maxStart%STEP);
 if(listStart<0)listStart=0;
 for(let row=0;row<ROWS;row++){
  const index=list[listStart+row],track=index===undefined?null:tracks[index];
  visible("item"+row,!!track);if(!track)continue;
  text("item"+row,track.name);
  style("item"+row,selected===index?{borderColor:"$accent",borderWidth:2}:{borderColor:"$border",borderWidth:1});
 }
}
function applyFilter(value){
 filterQuery=String(value==null?(Ouse.ui.getText("filter")||""):value);
 listStart=0;settle=2;
 if(Ouse.ui.scrollTo)Ouse.ui.scrollTo("sdList",0);
 if(page==="library"){paintList();refresh();}
}
function pollScroll(){
 if(page!=="library"||libraryTab!=="sd"||!Ouse.ui.getScroll||!Ouse.ui.scrollTo)return;
 if(settle){--settle;return;}
 const scroll=Ouse.ui.getScroll("sdList");if(!scroll||!scroll.height||scroll.pending)return;
 const list=matchList();
 if(listStart>0&&scroll.y<ROW_H){
  const step=Math.min(STEP,listStart);listStart-=step;settle=2;
  Ouse.ui.scrollTo("sdList",Math.max(0,scroll.y+step*ROW_H));paintList();return;
 }
 if(listStart+ROWS<list.length&&scroll.bottom<ROW_H*2){
  const step=Math.min(STEP,list.length-(listStart+ROWS));if(step<=0)return;
  listStart+=step;settle=2;
  Ouse.ui.scrollTo("sdList",Math.max(0,scroll.y-step*ROW_H));paintList();
 }
}
function refresh(){
 const changed=syncLibrary();if(changed&&page==="library"){bindPage();return;}
 const media=Ouse.media.get();
 if(wanted&&!Ouse.background.isActive())wanted=false;
 if(media&&!media.active){
  if(pending>=0){const next=pending;pending=-1;play(next);return;}
  if(media.taskId===taskId&&handled!==taskId){
   handled=taskId;const task=Ouse.tasks.get(taskId);
   if(wanted&&task&&task.state==="succeeded"&&!media.errorCode&&mode!==0){const next=mode===2?selected:selected+1;if(next<tracks.length){play(next);return;}}
   endLease();
  }
 }
 if(page==="library"){
  paintTabs();
  const sd=filesGet();
  const note=downloadId&&sd.downloadTaskId===downloadId?"已下载 "+Math.floor(sd.downloadBytes/1024)+" KiB":lastResult;
  text("netStatus",libraryTab==="net"?note:"");visible("netStatus",libraryTab==="net"&&!!note);
  if(libraryTab==="sd"){
   const list=matchList();
   let message="";
   if(!tracks.length)message=cardReady?"SD 卡里没有 WAV":"未检测到 SD 卡";
   else if(!list.length)message="没有匹配的曲目";
   text("listStatus",message);visible("listStatus",!!message);
   paintList();
  }
  return;
 }
 text("track",tracks[selected]?tracks[selected].name:"没有可播放的音乐");text("state",status(media));
 text("time",media?fmt(media.positionMs)+" / "+fmt(media.durationMs):"0:00 / 0:00");
 const progress=media&&media.durationMs?Math.min(100,Math.floor(media.positionMs*100/media.durationMs)):0;
 if(cache.progress!==progress){cache.progress=progress;Ouse.ui.setValue("progress",progress);}
 text("pause",media&&media.active&&media.paused?"继续":"暂停");text("mode",["单曲播放","顺序播放","单曲循环"][mode]);
 Ouse.ui.setEnabled("play",loaded&&tracks.length>0);Ouse.ui.setEnabled("pause",!!(media&&media.active));
 Ouse.ui.setEnabled("previous",loaded&&selected>0);Ouse.ui.setEnabled("next",loaded&&selected+1<tracks.length);
}
function bind(id,fn){Ouse.ui.on(id,"click",()=>{fn();});}
function bindPage(){
 cache=Object.create(null);
 if(page==="player"){
  bind("play",()=>{const media=Ouse.media.get();if(media&&media.active){if(media.paused){if(Ouse.media.resume())lease();}else if(Ouse.media.pause())endLease();refresh();}else play(selected);});
  bind("previous",()=>play(selected-1));bind("next",()=>play(selected+1));
  bind("pause",()=>{const media=Ouse.media.get();if(!media||!media.active)return;if(media.paused){if(Ouse.media.resume())lease();}else if(Ouse.media.pause())endLease();refresh();});
  bind("stop",()=>{pending=-1;endLease();Ouse.media.stop();});
  bind("mode",()=>{mode=(mode+1)%3;persist();refresh();});bind("library",()=>Ouse.navigation.push("library"));refresh();
 }else{
  bind("back",()=>Ouse.navigation.back());
  bind("tabSd",()=>{libraryTab="sd";listStart=0;settle=2;if(Ouse.ui.scrollTo)Ouse.ui.scrollTo("sdList",0);bindPage();});
  bind("tabNet",()=>{libraryTab="net";bindPage();});
  bind("rescan",()=>{if(Ouse.files)Ouse.files.refresh();lastResult="";});
  Ouse.ui.on("filter","change",value=>applyFilter(value));
  for(let row=0;row<ROWS;row++)bind("item"+row,()=>{const index=matchList()[listStart+row];if(index===undefined)return;play(index);Ouse.navigation.back();});
  bind("download",()=>{
   const name=Ouse.ui.getText("name").trim(),url=Ouse.ui.getText("url").trim();
   if(!/^[A-Za-z0-9_-]{1,64}$/.test(name)||!/^https?:\/\/[^\s]+$/.test(url)){lastResult="文件名用英文或数字，地址用 http 或 https";refresh();return;}
   try{downloadId=Ouse.files?Ouse.files.download({url,path:"Music/"+name+".wav"}):0;}catch(error){downloadId=0;}
   lastResult=downloadId?"正在下载到 SD 卡":"无法下载";refresh();
  });
  bind("cancel_download",()=>{if(downloadId)Ouse.tasks.cancel(downloadId);});
  paintTabs();refresh();
 }
}
Ouse.navigation.onChange(event=>{page=event.page;if(page==="library"){listStart=0;settle=2;}bindPage();resizePlayer();});
Ouse.tasks.onComplete(event=>{if(event.id===downloadId){downloadId=0;lastResult=event.state==="succeeded"?"下载完成":("下载未完成："+(event.error||event.state));}refresh();});
Ouse.notifications.onOpen(()=>{if(page!=="player")Ouse.navigation.back();refresh();});
Ouse.timers.setInterval(()=>{pollScroll();refresh();},200);
globalThis.app={onResume(){resizePlayer();libraryRevision=-1;refresh();openExternal();},onClose(){pending=-1;endLease();Ouse.media.stop();}};
bindPage();
resizePlayer();
Ouse.storage.get("library").then(value=>{
 if(value&&(value.version===1||value.version===2)&&Array.isArray(value.tracks)){
  remote=value.tracks.filter(track=>track&&typeof track.name==="string"&&typeof track.url==="string"&&/^https?:\/\/[^\s]+$/.test(track.url)&&track.url.length<=256).slice(0,8).map(track=>({name:track.name.slice(0,24),url:track.url}));
  mode=Number.isInteger(value.mode)&&value.mode>=0&&value.mode<=2?value.mode:0;
 }libraryRevision=-1;syncLibrary();if(value&&value.selectedPath){const index=tracks.findIndex(track=>(track.path||track.url)===value.selectedPath);if(index>=0)selected=index;}loaded=true;bindPage();openExternal();
},()=>{loaded=true;bindPage();openExternal();});
