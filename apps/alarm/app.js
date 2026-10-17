"use strict";
const OFFSET=480,SLOTS=["a1","a2","a3","a4"];
let page="home",editing="",pending=0,returnOnSuccess=false,cache=Object.create(null),message="";
function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function text(id,value){value=String(value);if(cache[id]!==value){cache[id]=value;Ouse.ui.setText(id,value);}}
function pad(n){return (n<10?"0":"")+n;}
function parts(ms){const mins=Math.floor((ms+OFFSET*60000)/60000);return {h:Math.floor(mins/60)%24,m:((mins%60)+60)%60};}
function nextOnce(h,m,now){const local=now+OFFSET*60000;let at=Math.floor(local/86400000)*86400000+(h*60+m)*60000;if(at<=local)at+=86400000;return at-OFFSET*60000;}
function weekdays(mask){if(!mask)return "一次";if(mask===127)return "每天";if(mask===62)return "工作日";return [1,2,3,4,5,6,0].filter(d=>mask&(1<<d)).map(d=>"一二三四五六日".charAt((d+6)%7)).join(" ");}
function when(alarm){const t=alarm.weekdays?{h:Math.floor(alarm.minuteOfDay/60),m:alarm.minuteOfDay%60}:parts(alarm.atMs);return pad(t.h)+":"+pad(t.m);}
function note(value){message=value;text("status",value);visible("status",!!value);}
function busy(value){
 if(page==="home"){for(let i=0;i<4;i++){Ouse.ui.setEnabled("edit"+i,!value);Ouse.ui.setEnabled("enable"+i,!value);}}
 else{Ouse.ui.setEnabled("save",!value);Ouse.ui.setEnabled("remove",!value&&!!editing);}
}
function submit(id,back){if(!id){note("请稍后重试");return;}pending=id;returnOnSuccess=!!back;note("");busy(true);if(page==="home")Ouse.ui.setEnabled("add",false);}
function refresh(){
 const list=Ouse.alarms.list();if(page!=="home")return;
 const clock=Ouse.clock.get(),t=parts(clock.utcMs);text("clock",clock.valid?pad(t.h)+":"+pad(t.m):"时间无效");
 for(let i=0;i<4;i++){const alarm=list[i];visible("row"+i,!!alarm);if(!alarm)continue;const state=alarm.ringing?"  响铃":alarm.missed?"  已错过":"";text("edit"+i,when(alarm)+"  "+alarm.title+(alarm.weekdays?"  "+weekdays(alarm.weekdays):"")+state);Ouse.ui.setValue("enable"+i,alarm.enabled);}
 visible("empty",list.length===0);Ouse.ui.setEnabled("add",!pending&&list.length<4);
 const ringing=list.find(a=>a.ringing);visible("ringing",!!ringing);if(ringing)text("ringTitle","正在响铃："+ringing.title);
}
function bind(id,fn,event){Ouse.ui.on(id,event||"click",()=>{try{fn();}catch(error){note(String(error.message||error));}});}
function edit(id){editing=id;Ouse.navigation.push("editor");}
function bindPage(){
 cache=Object.create(null);
 if(page==="home"){
  bind("add",()=>edit(""));
  for(let i=0;i<4;i++){
   bind("edit"+i,()=>{const alarm=Ouse.alarms.list()[i];if(alarm)edit(alarm.id);});
   bind("enable"+i,()=>{const alarm=Ouse.alarms.list()[i];if(!alarm)return;if(!alarm.enabled&&!alarm.weekdays&&alarm.atMs<=Ouse.clock.get().utcMs){edit(alarm.id);return;}submit(Ouse.alarms.setEnabled(alarm.id,!alarm.enabled),false);},"change");
  }
  bind("dismiss",()=>{const a=Ouse.alarms.list().find(x=>x.ringing);if(a)submit(Ouse.alarms.dismiss(a.id),false);});
  bind("snooze",()=>{const a=Ouse.alarms.list().find(x=>x.ringing);if(a)submit(Ouse.alarms.snooze(a.id,5),false);});refresh();
 }else{
  const alarm=Ouse.alarms.list().find(a=>a.id===editing),t=alarm?parts(alarm.atMs):{h:7,m:30};
  text("heading",alarm?"编辑闹钟":"新建闹钟");Ouse.ui.setText("name",alarm?alarm.title:"");
  Ouse.ui.setValue("hour",alarm&&alarm.weekdays?Math.floor(alarm.minuteOfDay/60):t.h);const minute=alarm&&alarm.weekdays?alarm.minuteOfDay%60:t.m;Ouse.ui.setValue("tens",Math.floor(minute/10));Ouse.ui.setValue("ones",minute%10);
  for(let i=0;i<7;i++)Ouse.ui.setValue("d"+i,!!(alarm&&(alarm.weekdays&(1<<i))));
  bind("back",()=>Ouse.navigation.back());
  bind("save",()=>{
   const clock=Ouse.clock.get();if(!clock.valid){note("系统时间无效，请先连接 Wi-Fi 校时");return;}
   const id=editing||SLOTS.find(s=>!Ouse.alarms.list().some(a=>a.id===s));if(!id){note("最多 4 个闹钟");return;}
   const hour=Ouse.ui.getValue("hour"),minute=Ouse.ui.getValue("tens")*10+Ouse.ui.getValue("ones");let mask=0;for(let i=0;i<7;i++)if(Ouse.ui.getValue("d"+i))mask|=1<<i;
   const options={id,title:(Ouse.ui.getText("name").trim()||"闹钟").slice(0,24),payload:id,weekdays:mask,minuteOfDay:hour*60+minute,utcOffsetMinutes:OFFSET};if(!mask)options.atMs=nextOnce(hour,minute,clock.utcMs);submit(Ouse.alarms.set(options),true);
  });bind("remove",()=>{if(editing)submit(Ouse.alarms.remove(editing),true);});
 }busy(!!pending);
}
Ouse.navigation.onChange(event=>{page=event.page;bindPage();});Ouse.notifications.onOpen(()=>{if(page!=="home")Ouse.navigation.back();refresh();});
Ouse.tasks.onComplete(task=>{
 if(task.id===pending){pending=0;const back=returnOnSuccess;returnOnSuccess=false;if(task.state==="succeeded"){note("");if(back&&page==="editor"){Ouse.navigation.back();return;}}else note("操作失败："+task.error);busy(false);}refresh();
});
Ouse.timers.setInterval(refresh,1000);globalThis.app={onResume:refresh};bindPage();
