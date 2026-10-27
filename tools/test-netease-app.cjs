const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const handlers = {}, texts = {}, timers = [];
const link = {enabled:true, ready:true, connected:false, session:0, consumerReady:false};
let opens = 0, closes = 0, reports = 0, reject = false;
const context = vm.createContext({Ouse:{
    ui:{on:(id,event,handler)=>{handlers[id]=handler;},show(){},hide(){},layout(){},setText:(id,text)=>{texts[id]=text;},setImage(){}},
    display:{get:()=>({contentWidthDp:320,contentHeightDp:416}),onChange(){}},
    permissions:{get:()=>"granted",request:()=>"granted"},
    timers:{setInterval:handler=>timers.push(handler),setTimeout(){}},
    bluetooth:{get:()=>({...link}),open:()=>{++opens;return reject?0:(link.session=101);},
        close:()=>{++closes;link.session=0;},consumer:()=>{++reports;return true;},release(){}}
}});
vm.runInContext(fs.readFileSync("apps/netease/app.js","utf8"),context);
const tick = ()=>timers[0]();
tick();assert.equal(opens,0);assert.equal(texts.status,"尚未连接设备");
handlers.toggle();assert.equal(reports,0);
link.enabled=false;tick();assert.equal(texts.status,"蓝牙已关闭");
link.enabled=true;link.connected=true;tick();assert.equal(opens,1);assert.equal(texts.status,"等待输入服务");
tick();handlers.toggle();assert.equal(reports,0);
link.consumerReady=true;tick();assert.equal(texts.status,"已连接");handlers.toggle();assert.equal(reports,1);
link.connected=false;link.consumerReady=false;tick();assert.equal(texts.status,"尚未连接设备");assert(!Object.hasOwn(texts,"now"));assert(!Object.hasOwn(texts,"toggle"));
context.app.onPause();context.app.onClose();assert.equal(closes,2);
context.app.onResume();link.connected=true;link.busy=true;reject=true;tick();assert.equal(texts.status,"连接被占用");
link.closing=true;tick();assert.equal(texts.status,"连接释放中");
link.busy=false;link.closing=false;tick();assert.equal(texts.status,"连接暂不可用");
console.log("PASS NetEase: disconnected/off/lease-vs-HID readiness, link loss, repeated close and distinct busy/closing errors");
const page=JSON.parse(fs.readFileSync("apps/netease/page.json","utf8"));
function find(node,id){return node.id===id?node:(node.children||[]).map(child=>find(child,id)).find(Boolean);}
assert(!find(page.root,"now"));assert.equal(find(page.root,"toggle").text,"播放/暂停");
