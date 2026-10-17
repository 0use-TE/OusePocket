"use strict";
const MAX=12, OFFSET=8*3600*1000;
let items=[], loaded=false, saving=false, dirty=false, editing=-1;
let draft={y:2026,m:1,d:1,h:0,n:0};

function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function text(id,value){Ouse.ui.setText(id,String(value));}
function pad(n){return n<10?"0"+n:String(n);}
function daysInMonth(y,m){return new Date(Date.UTC(y,m,0)).getUTCDate();}
function nowParts(){
 const clock=Ouse.clock&&Ouse.clock.get?Ouse.clock.get():null;
 const ms=clock&&Number.isFinite(clock.utcMs)?clock.utcMs:0;
 if(!ms)return {y:2026,m:1,d:1,h:0,n:0};
 const day=new Date(ms+OFFSET);
 return {
  y:day.getUTCFullYear(),
  m:day.getUTCMonth()+1,
  d:day.getUTCDate(),
  h:day.getUTCHours(),
  n:day.getUTCMinutes()
 };
}
function clampDraft(){
 if(draft.y<2000)draft.y=2000;
 if(draft.y>2100)draft.y=2100;
 if(draft.m<1)draft.m=12;
 if(draft.m>12)draft.m=1;
 const max=daysInMonth(draft.y,draft.m);
 if(draft.d<1)draft.d=max;
 if(draft.d>max)draft.d=1;
 if(draft.h<0)draft.h=23;
 if(draft.h>23)draft.h=0;
 if(draft.n<0)draft.n=59;
 if(draft.n>59)draft.n=0;
}
function formatAt(at){
 if(!at)return "";
 return at.y+"年"+at.m+"月"+at.d+"日 "+pad(at.h)+":"+pad(at.n);
}
function copyAt(at){return {y:at.y|0,m:at.m|0,d:at.d|0,h:at.h|0,n:at.n|0};}
function validAt(at){
 return at&&Number.isInteger(at.y)&&Number.isInteger(at.m)&&Number.isInteger(at.d)&&
  Number.isInteger(at.h)&&Number.isInteger(at.n)&&
  at.y>=2000&&at.y<=2100&&at.m>=1&&at.m<=12&&at.d>=1&&at.d<=31&&at.h>=0&&at.h<=23&&at.n>=0&&at.n<=59;
}

function persist(){
 dirty=true;if(saving)return;saving=true;dirty=false;
 Ouse.storage.set("todos",{version:4,items:items}).then(()=>{
  saving=false;if(dirty)persist();
 },()=>{saving=false;dirty=false;});
}

function paintDraft(){
 clampDraft();
 text("valY",draft.y+"年");
 text("valM",draft.m+"月");
 text("valD",draft.d+"日");
 text("valH",pad(draft.h)+"时");
 text("valN",pad(draft.n)+"分");
 text("whenBtn",formatAt(draft));
}

function paint(){
 const done=items.filter(it=>it.done).length;
 Ouse.ui.setEnabled("add",loaded&&items.length<MAX);
 Ouse.ui.setEnabled("clear",loaded&&done>0);
 for(let i=0;i<MAX;i++){
  const it=loaded?items[i]:null;
  if(!it){visible("row"+i,false);continue;}
  visible("row"+i,true);
  text("item"+i,it.text);
  text("time"+i,formatAt(it.at)||it.when||"");
  Ouse.ui.setValue("cb"+i,!!it.done);
  Ouse.ui.setStyle("item"+i,{textColor:it.done?"$secondary":"$text"});
 }
}

function openEdit(index){
 editing=index;
 if(index<0){
  Ouse.ui.setText("name","");
  draft=nowParts();
 }else{
  Ouse.ui.setText("name",items[index].text||"");
  draft=validAt(items[index].at)?copyAt(items[index].at):nowParts();
 }
 paintDraft();
 Ouse.ui.setEnabled("remove",index>=0);
 Ouse.ui.show("editSheet");
}

function bump(field,delta){
 if(field==="y")draft.y+=delta;
 else if(field==="m")draft.m+=delta;
 else if(field==="d")draft.d+=delta;
 else if(field==="h")draft.h+=delta;
 else if(field==="n")draft.n+=delta;
 paintDraft();
}

Ouse.ui.on("add","click",()=>{if(loaded&&items.length<MAX)openEdit(-1);});
Ouse.ui.on("clear","click",()=>{
 if(!loaded)return;
 items=items.filter(it=>!it.done);
 paint();
 persist();
});
Ouse.ui.on("whenBtn","click",()=>{paintDraft();Ouse.ui.show("timeSheet");});
Ouse.ui.on("timeDone","click",()=>{paintDraft();Ouse.ui.hide("timeSheet");});
Ouse.ui.on("decY","click",()=>bump("y",-1));
Ouse.ui.on("incY","click",()=>bump("y",1));
Ouse.ui.on("decM","click",()=>bump("m",-1));
Ouse.ui.on("incM","click",()=>bump("m",1));
Ouse.ui.on("decD","click",()=>bump("d",-1));
Ouse.ui.on("incD","click",()=>bump("d",1));
Ouse.ui.on("decH","click",()=>bump("h",-1));
Ouse.ui.on("incH","click",()=>bump("h",1));
Ouse.ui.on("decN","click",()=>bump("n",-1));
Ouse.ui.on("incN","click",()=>bump("n",1));
Ouse.ui.on("save","click",()=>{
 if(!loaded)return;
 const value=Ouse.ui.getText("name").trim().slice(0,48);
 if(!value){Ouse.ui.hide("editSheet");return;}
 clampDraft();
 const at=copyAt(draft);
 const when=formatAt(at);
 if(editing<0)items.unshift({text:value,at:at,when:when,done:false});
 else {items[editing].text=value;items[editing].at=at;items[editing].when=when;}
 if(items.length>MAX)items=items.slice(0,MAX);
 Ouse.ui.hide("timeSheet");
 Ouse.ui.hide("editSheet");
 paint();
 persist();
});
Ouse.ui.on("remove","click",()=>{
 if(!loaded||editing<0)return;
 items.splice(editing,1);
 Ouse.ui.hide("timeSheet");
 Ouse.ui.hide("editSheet");
 paint();
 persist();
});
for(let i=0;i<MAX;i++){(function(row){
 Ouse.ui.on("cb"+row,"change",()=>{
  if(!loaded||row>=items.length)return;
  items[row].done=!!Ouse.ui.getValue("cb"+row);
  paint();
  persist();
 });
 Ouse.ui.on("item"+row,"click",()=>{if(row<items.length)openEdit(row);});
})(i);}

paint();
Ouse.storage.get("todos").then(value=>{
 if(value&&(value.version===4||value.version===3||value.version===2||value.version===1)&&Array.isArray(value.items)){
  items=value.items.filter(it=>it&&typeof it.text==="string").map(it=>{
   const row={text:String(it.text).slice(0,48),done:!!it.done,when:"",at:null};
   if(validAt(it.at)){row.at=copyAt(it.at);row.when=formatAt(row.at);}
   else if(typeof it.when==="string")row.when=it.when.slice(0,32);
   return row;
  }).slice(0,MAX);
 }
 loaded=true;
 paint();
},()=>{loaded=true;paint();});
