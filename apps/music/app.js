"use strict";
const SAMPLE={name:"致爱丽丝 · 30 秒",asset:"sample"};
let tracks=[SAMPLE],selected=0,mode=0,page="player",loaded=false;
let wanted=false,pending=-1,taskId=0,handled=0,saving=false,dirty=false;
let cache=Object.create(null);
function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function text(id,value){value=String(value);if(cache[id]!==value){cache[id]=value;Ouse.ui.setText(id,value);}}
function fmt(ms){const s=Math.floor(Math.max(0,ms)/1000);return Math.floor(s/60)+":"+(s%60<10?"0":"")+(s%60);}
function persist(){
 dirty=true;if(saving)return;saving=true;dirty=false;
 Ouse.storage.set("library",{version:1,tracks:tracks.slice(1),selected,mode}).then(()=>{saving=false;if(dirty)persist();},()=>{saving=false;dirty=false;});
}
function endLease(){wanted=false;Ouse.background.end();}
function lease(){wanted=Ouse.background.begin("music-playlist");}
function play(index){
 if(!loaded||index<0||index>=tracks.length)return;
 const media=Ouse.media.get();
 if(media&&media.active){pending=index;endLease();if(!Ouse.media.stop())pending=-1;return;}
 pending=-1;selected=index;handled=0;
 try{taskId=Ouse.media.play(tracks[index].asset?{asset:tracks[index].asset}:{url:tracks[index].url});}catch(error){return;}
 if(!taskId){endLease();return;}
 lease();persist();refresh();
}
function status(media){
 if(!media)return "尚未播放";
 if(media.errorCode)return "播放失败："+media.error;
 if(!media.active)return "已停止";
 if(media.interrupted)return "闹铃优先，等待恢复";
 if(media.paused)return "已暂停";
 return media.buffering?"正在缓冲":"正在播放";
}
function refresh(){
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
 if(page!=="player")return;
 text("track",tracks[selected].name);text("state",status(media));text("time",media?fmt(media.positionMs)+" / "+fmt(media.durationMs):"0:00 / 0:00");
 const progress=media&&media.durationMs?Math.min(100,Math.floor(media.positionMs*100/media.durationMs)):0;
 if(cache.progress!==progress){cache.progress=progress;Ouse.ui.setValue("progress",progress);}
 text("pause",media&&media.active&&media.paused?"继续":"暂停");text("mode",["单曲播放","顺序播放","单曲循环"][mode]);
 Ouse.ui.setEnabled("play",loaded);Ouse.ui.setEnabled("pause",!!(media&&media.active));
 Ouse.ui.setEnabled("previous",loaded&&selected>0);Ouse.ui.setEnabled("next",loaded&&selected+1<tracks.length);
}
function bind(id,fn){Ouse.ui.on(id,"click",()=>{fn();});}
function bindPage(){
 cache=Object.create(null);
 if(page==="player"){
  bind("play",()=>play(selected));bind("previous",()=>play(selected-1));bind("next",()=>play(selected+1));
  bind("pause",()=>{const media=Ouse.media.get();if(!media||!media.active)return;if(media.paused){if(Ouse.media.resume())lease();}else if(Ouse.media.pause())endLease();refresh();});
  bind("stop",()=>{pending=-1;endLease();Ouse.media.stop();});
  bind("mode",()=>{mode=(mode+1)%3;persist();refresh();});bind("library",()=>Ouse.navigation.push("library"));refresh();
 }else{
  bind("back",()=>Ouse.navigation.back());
  for(let i=0;i<8;i++){
   const track=tracks[i];visible("row"+i,!!track);if(!track)continue;
   text("item"+i,(selected===i?"> ":"")+track.name);bind("item"+i,()=>{play(i);Ouse.navigation.back();});Ouse.ui.setEnabled("remove"+i,i>0);
   bind("remove"+i,()=>{const media=Ouse.media.get();if(media&&media.active){text("listStatus","先停止播放，再修改列表");return;}tracks.splice(i,1);if(selected===i)selected=0;else if(selected>i)--selected;persist();bindPage();});
  }
  bind("add",()=>{
   const name=Ouse.ui.getText("name").trim(),url=Ouse.ui.getText("url").trim();
   if(tracks.length>=8){text("listStatus","最多保存 8 首，含内置片段");return;}
   if(!/^https?:\/\/[^\s]+$/.test(url)||url.length>256){text("listStatus","请填写有效的 HTTP 或 HTTPS 地址");return;}
   if(tracks.some(t=>t.url===url)){text("listStatus","这个地址已经在列表中");return;}
   tracks.push({name:(name||"网络音频").slice(0,24),url});persist();bindPage();text("listStatus","已加入列表");Ouse.ui.setText("url","");Ouse.ui.setText("name","");
  });Ouse.ui.setEnabled("add",loaded);
 }
}
Ouse.navigation.onChange(event=>{page=event.page;bindPage();});Ouse.tasks.onComplete(()=>refresh());
Ouse.notifications.onOpen(()=>{if(page!=="player")Ouse.navigation.back();refresh();});Ouse.timers.setInterval(refresh,1000);
globalThis.app={onResume:refresh,onClose(){pending=-1;endLease();}};bindPage();
Ouse.storage.get("library").then(value=>{
 if(value&&value.version===1&&Array.isArray(value.tracks)){
  tracks=[SAMPLE].concat(value.tracks.filter(t=>t&&typeof t.name==="string"&&typeof t.url==="string"&&/^https?:\/\/[^\s]+$/.test(t.url)&&t.url.length<=256).slice(0,7).map(t=>({name:t.name.slice(0,24),url:t.url})));
  selected=Number.isInteger(value.selected)&&value.selected>=0&&value.selected<tracks.length?value.selected:0;
  mode=Number.isInteger(value.mode)&&value.mode>=0&&value.mode<=2?value.mode:0;
 }loaded=true;bindPage();
},()=>{loaded=true;bindPage();});
