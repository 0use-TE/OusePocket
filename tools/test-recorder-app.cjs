const assert=require("node:assert/strict");
const fs=require("node:fs");
const vm=require("node:vm");
function load(ready,permission="granted"){
 const state={active:false,stopping:false,busy:false,more:false,entries:[],durationMs:0,ended:1792300000,path:"",offset:0};
 const card={ready,present:ready},media={active:false},texts={},enabled={},shown=new Set(),handlers={},requests=[],intervals=[];
 const settings=[];let complete,identifier=100,stops=0,rowWrites=0;const permissions={microphone:permission,sharedDelete:permission},cleared=new Set();
 const request=(operation,...args)=>{requests.push([operation,...args]);return ++identifier;};
 const api={system:{openSettings:key=>settings.push(key)},ui:{setText(key,value){texts[key]=value;if(key.startsWith("entry"))++rowWrites;},getText:key=>texts[key],setEnabled(key,value){enabled[key]=value;},show:key=>shown.add(key),hide:key=>shown.delete(key),on(key,event,handler){handlers[key]=handler;},scrollTo(){}},
 files:{get:()=>card},recorder:{get:()=>state,list:offset=>request("list",offset),start:()=>request("start"),stop(){++stops;state.stopping=true;return true;},rename:(...args)=>request("rename",...args),remove:path=>request("remove",path)},
 permissions:{get:name=>permissions[name],request:name=>permissions[name]},
 media:{get:()=>media,stop(){media.active=false;},play(value){requests.push(["play",value.path]);media.active=true;return 500;}},timers:{setInterval(fn){intervals.push(fn);return intervals.length;},clear(id){cleared.add(id);}},tasks:{onComplete(fn){complete=fn;}}};
 const context=vm.createContext({Ouse:api});vm.runInContext(fs.readFileSync("apps/recorder/app.js","utf8"),context);
 return {settings,state,card,media,texts,enabled,shown,requests,permissions,rowWrites:()=>rowWrites,timerCount:()=>intervals.length-cleared.size,lifecycle:context.app,stops:()=>stops,click:key=>handlers[key](),tick:()=>intervals.slice().forEach((fn,index)=>{if(!cleared.has(index+1))fn();}),finish(ok=true){complete({id:identifier,state:ok?"succeeded":"failed"});}};
}
{
 const app=load(false);assert(app.shown.has("dialog"));assert.equal(app.requests.length,0);assert.equal(app.enabled.record,false);
 app.click("record");assert.equal(app.requests.length,0);
 app.card.ready=app.card.present=true;app.tick();assert.deepEqual(app.requests.at(-1),["list",0]);app.finish();
 app.click("record");assert.deepEqual(app.requests.at(-1),["start"]);app.click("record");assert.equal(app.requests.filter(item=>item[0]==="start").length,1);
 app.state.active=true;app.state.durationMs=65000;app.tick();assert.equal(app.texts.elapsed,"01:05");assert.equal(app.texts.record,"结束");
 app.click("record");assert.equal(app.stops(),1);assert.equal(app.enabled.record,false);
 app.state.active=false;app.state.stopping=false;app.state.path="Recordings/REC_1792300000_test.wav";app.finish();assert(app.shown.has("nameDialog"));
 app.texts.name="我的录音";app.click("saveName");assert.deepEqual(app.requests.at(-1),["rename",app.state.path,"我的录音"]);app.finish();assert(!app.shown.has("nameDialog"));assert.deepEqual(app.requests.at(-1),["list",0]);
 app.state.entries=[{path:"Recordings/我的录音.wav",durationMs:65000,ended:1792300000}];app.finish();assert(app.texts.entry0.includes("01:05"));assert(app.texts.entry0.includes("2026-"));
 const rendered=app.rowWrites();for(let index=0;index<40;++index)app.tick();assert.equal(app.rowWrites(),rendered);
 app.click("entry0");app.click("play");assert.deepEqual(app.requests.at(-1),["play",app.state.entries[0].path]);
 app.click("remove");assert(app.shown.has("deleteDialog"));assert.notEqual(app.requests.at(-1)[0],"remove");app.click("confirmDelete");assert.deepEqual(app.requests.at(-1),["remove",app.state.entries[0].path]);app.finish();app.finish();
 app.lifecycle.onPause();app.lifecycle.onClose();assert(app.stops()>=3);assert(!app.media.active);
}
{
 const app=load(true);app.finish();app.click("record");app.state.path="Recordings/default.wav";app.finish();app.click("keepName");assert(!app.shown.has("nameDialog"));assert.deepEqual(app.requests.at(-1),["list",0]);
}
{
 const app=load(true);app.finish();app.click("record");app.finish(false);assert(app.shown.has("dialog"));assert(app.texts.detail.includes("SD 卡"));
 app.card.present=false;app.tick();assert.equal(app.enabled.record,false);
}
{
 const app=load(true);app.finish();app.media.active=true;app.click("record");assert.equal(app.requests.filter(item=>item[0]==="start").length,0);app.tick();assert.deepEqual(app.requests.at(-1),["start"]);
}
{
 const app=load(true,"ask");app.finish();app.click("record");assert.equal(app.timerCount(),2);assert(!app.requests.some(item=>item[0]==="start"));
 app.permissions.microphone="granted";app.tick();assert.deepEqual(app.requests.at(-1),["start"]);assert.equal(app.timerCount(),1);app.tick();assert.equal(app.requests.filter(item=>item[0]==="start").length,1);
}
{
 const app=load(true,"ask");app.finish();app.click("record");app.permissions.microphone="denied";app.tick();assert(app.shown.has("dialog"));assert.equal(app.timerCount(),1);assert(!app.requests.some(item=>item[0]==="start"));
}
{
 const app=load(true,"ask");app.finish();app.click("record");app.lifecycle.onPause();app.permissions.microphone="granted";app.tick();assert(!app.requests.some(item=>item[0]==="start"));assert.equal(app.timerCount(),1);
}
{
 const app=load(true,"ask");app.finish();app.click("record");for(let index=0;index<150;++index)app.tick();assert(app.shown.has("dialog"));assert.equal(app.timerCount(),1);assert(!app.requests.some(item=>item[0]==="start"));
}
{
 const app=load(true,"denied");app.finish();app.click("record");assert(app.shown.has("permissionSettings"));app.click("permissionSettings");assert.equal(app.settings.at(-1),"permissions");assert(!app.requests.some(item=>item[0]==="start"));
}
console.log("PASS recorder: SD gate, duplicate start, record lifecycle and permission ask-to-grant/deny/timeout/pause with real clear API and no leaked permission timer");
