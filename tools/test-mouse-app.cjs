const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm");
const handlers={},reports=[],timers=[],texts={};let releases=0;
const link={enabled:true,ready:true,connected:true,mouseReady:true,session:0};
const context=vm.createContext({Ouse:{ui:{on:(id,event,fn)=>{handlers[id+":"+event]=fn;},show(){},hide(){},setText:(id,text)=>{texts[id]=text;}},permissions:{get:()=>"granted",request(){}},
    timers:{setInterval:fn=>timers.push(fn),setTimeout(){}},bluetooth:{get:()=>({...link}),open:()=>link.session=11,close(){link.session=0;},release(){++releases;},mouse:(...report)=>{reports.push(report);return true;}}}});
vm.runInContext(fs.readFileSync("apps/mouse/app.js","utf8"),context);
timers[0]();timers[0]();assert.equal(texts.status,"已连接");
handlers["pad:pointerdown"]();handlers["pad:pointermove"]({dx:10,dy:-8});assert.deepEqual(reports.at(-1),[0,10,-8,0]);
const moved=reports.length;handlers["pad:pointerup"]();assert.equal(reports.length,moved);
handlers["left:pointerdown"]();assert.deepEqual(reports.at(-1),[1,0,0,0]);
handlers["left:pointermove"]({dx:200,dy:4});assert.deepEqual(reports.at(-1),[1,127,4,0]);
timers[0]();assert.deepEqual(reports.at(-1),[1,0,0,0]);handlers["left:pointercancel"]();assert.equal(releases,1);
link.connected=false;link.mouseReady=false;timers[0]();assert.equal(texts.status,"尚未连接设备");
context.app.onPause();context.app.onClose();assert.equal(link.session,0);
console.log("PASS mouse: pointer movement, no accidental drag click, held drag/clamp/keepalive/cancel, link loss and lifecycle release");
