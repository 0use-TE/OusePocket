"use strict";
let pending=0,kind="",entries=[],offset=0,selected=-1,naming="",namingNew=false,noticed=false,paused=false,startWanted=false;
function show(id,value){if(value)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function ready(){const card=Ouse.files.get();return card.ready&&card.present;}
function popup(message){Ouse.ui.setText("detail",message);Ouse.ui.show("dialog");}
function duration(milliseconds){const seconds=Math.floor(milliseconds/1000);return String(Math.floor(seconds/60)).padStart(2,"0")+":"+String(seconds%60).padStart(2,"0");}
function ended(seconds){if(seconds<1577836800)return "结束时间未知";const date=new Date(seconds*1000),pad=value=>String(value).padStart(2,"0");return date.getFullYear()+"-"+pad(date.getMonth()+1)+"-"+pad(date.getDate())+" "+pad(date.getHours())+":"+pad(date.getMinutes());}
function name(path){return path.slice(path.lastIndexOf("/")+1).replace(/\.wav$/i,"");}
function list(start=offset){if(pending||!ready())return;pending=Ouse.recorder.list(start);kind="list";if(!pending)popup("录音列表暂不可用。");}
function rename(path,isNew){naming=path;namingNew=isNew;Ouse.ui.setText("name",isNew?"录音_"+ended(Ouse.recorder.get().ended).replace(/[- :]/g,""):name(path));Ouse.ui.show("nameDialog");}
let permissionPending=false;
function authorize(name,action){
 if(permissionPending)return;
 if(!Ouse.permissions){action();return;}
 const state=Ouse.permissions.request(name);
 if(state==="granted"){action();return;}if(state==="denied"){popup("权限未开启。");return;}
 permissionPending=true;let ticks=0;const timer=Ouse.timers.setInterval(()=>{const result=Ouse.permissions.get(name);if(result!=="ask"||++ticks>=150){Ouse.timers.clear(timer);permissionPending=false;if(result==="granted"&&!paused)action();else if(!paused)popup("权限未开启。");}},100);
}
function start(){authorize("microphone",()=>{if(pending||paused||!ready())return;pending=Ouse.recorder.start();kind="record";if(!pending)popup("无法开始录音，请检查麦克风占用和 SD 卡空间。");refresh();});}
function refresh(){
 const state=Ouse.recorder.get();if(!state)return;
 Ouse.ui.setText("record",state.active?"结束":"录音");Ouse.ui.setText("elapsed",duration(state.durationMs));
 Ouse.ui.setEnabled("record",!state.stopping&&(state.active||(!pending&&!startWanted&&!state.busy&&ready())));
 Ouse.ui.setEnabled("previous",!pending&&!state.active&&offset>0);Ouse.ui.setEnabled("next",!pending&&!state.active&&state.more);
 show("paging",offset>0||state.more);show("empty",!entries.length&&!state.active&&!pending&&ready());
 for(let index=0;index<12;++index){show("entry"+index,!!entries[index]);if(entries[index])Ouse.ui.setText("entry"+index,name(entries[index].path)+"\n"+duration(entries[index].durationMs)+"  "+ended(entries[index].ended));Ouse.ui.setEnabled("entry"+index,!pending&&!state.active&&ready());}
}
Ouse.ui.on("record","click",()=>{
 const state=Ouse.recorder.get();if(state.active){Ouse.recorder.stop();refresh();return;}
 if(pending||startWanted)return;if(!ready()){popup("请插入 SD 卡后再录音。");return;}
 const media=Ouse.media.get();if(media&&media.active){startWanted=true;Ouse.media.stop();refresh();return;}start();
});
Ouse.ui.on("previous","click",()=>list(Math.max(0,offset-12)));Ouse.ui.on("next","click",()=>list(offset+12));
for(let index=0;index<12;++index)Ouse.ui.on("entry"+index,"click",()=>{if(pending||!entries[index])return;selected=index;Ouse.ui.setText("selectedName",name(entries[index].path));Ouse.ui.setText("selectedDate",ended(entries[index].ended));Ouse.ui.show("actions");});
Ouse.ui.on("play","click",()=>{if(!ready()){popup("SD 卡不可用。");return;}const media=Ouse.media.get();if(media&&media.active){Ouse.media.stop();return;}if(!Ouse.media.play({path:entries[selected].path}))popup("无法播放，请稍后重试。");});
Ouse.ui.on("rename","click",()=>{Ouse.media.stop();Ouse.ui.hide("actions");rename(entries[selected].path,false);});
Ouse.ui.on("remove","click",()=>{Ouse.media.stop();Ouse.ui.hide("actions");Ouse.ui.show("deleteDialog");});
Ouse.ui.on("closeActions","click",()=>{Ouse.media.stop();Ouse.ui.hide("actions");});
Ouse.ui.on("confirmDelete","click",()=>{if(pending||selected<0)return;Ouse.ui.hide("deleteDialog");const path=entries[selected].path;authorize("sharedDelete",()=>{if(pending||!ready())return;pending=Ouse.recorder.remove(path);kind="remove";if(!pending)popup("删除失败，请稍后重试。");});});
Ouse.ui.on("cancelDelete","click",()=>Ouse.ui.hide("deleteDialog"));
Ouse.ui.on("saveName","click",()=>{if(pending||!naming)return;const value=Ouse.ui.getText("name").trim();if(!value||value.length>24||/[\\/:*?"<>|]/.test(value)){popup("请输入不超过 24 字的有效名称。");return;}pending=Ouse.recorder.rename(naming,value);kind="rename";if(!pending)popup("重命名失败，请检查名称或 SD 卡。");});
Ouse.ui.on("keepName","click",()=>{Ouse.ui.hide("nameDialog");naming="";list(namingNew?0:offset);});
Ouse.ui.on("dismiss","click",()=>Ouse.ui.hide("dialog"));
Ouse.tasks.onComplete(event=>{
 if(event.id!==pending)return;pending=0;const state=Ouse.recorder.get();
 if(event.state!=="succeeded"){popup(kind==="record"?"录音未完成，请检查 SD 卡和可用空间。":"操作失败，请检查 SD 卡，名称不能重复。");refresh();return;}
 if(kind==="list"){entries=state.entries;offset=state.offset;Ouse.ui.scrollTo("recordings",0);}
 else if(kind==="record"){if(state.path&&!paused)rename(state.path,true);else list(0);}
 else{Ouse.ui.hide("nameDialog");naming="";list(kind==="remove"?Math.max(0,offset-12):0);}
 refresh();
});
Ouse.timers.setInterval(()=>{
 if(!ready()){startWanted=false;Ouse.recorder.stop();Ouse.media.stop();if(!noticed){noticed=true;popup("请插入 SD 卡后使用录音机。");}}
 else if(noticed){noticed=false;Ouse.ui.hide("dialog");list(0);}
 const media=Ouse.media.get();if(startWanted&&(!media||!media.active)&&!pending&&!paused){startWanted=false;start();}Ouse.ui.setText("play",media&&media.active?"停止":"播放");refresh();
},250);
globalThis.app={onPause(){paused=true;startWanted=false;Ouse.recorder.stop();Ouse.media.stop();},onResume(){paused=false;if(!pending)list();refresh();},onClose(){startWanted=false;Ouse.recorder.stop();Ouse.media.stop();}};
if(ready())list(0);else{noticed=true;popup("请插入 SD 卡后使用录音机。");}refresh();
