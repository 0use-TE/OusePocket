const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
function load(name,ready){
 const state={ready,present:ready,running:false,busy:false,inUse:false,error:"ESP_OK",recording:false,recordedMs:0,playing:false,entries:[],offset:0,more:false,path:""};
 const texts={},styles={},enabled={},shown=new Set(),handlers={},intervals=[],requests=[],launches=[],scroll={y:0,bottom:200,height:300,pending:false};let complete,navigate,id=100,stopped=0,photoStops=0;
 const ui={setText(key,value){texts[key]=value;},setStyle(key,value){styles[key]=value;},setEnabled(key,value){enabled[key]=value;},show(key){shown.add(key);},hide(key){shown.delete(key);},on(key,event,fn){handlers[key]=fn;},getScroll(){return scroll;},scrollTo(key,value){scroll.y=value;}};
 const request=(operation,value)=>{requests.push([operation,value]);if(state.inUse&&operation==="start")return false;state.busy=true;return ++id;};
 const api={ui,power:{get:()=>state},files:{get:()=>state},camera:{get:()=>state,start:()=>request("start"),capture:()=>request("capture"),record:()=>request("record"),recordStop:()=>request("recordStop"),stop(){++stopped;state.running=false;state.recording=false;}},
  photos:{get:()=>state,list:offset=>request("list",offset),open:value=>request("open",value),remove:value=>request("remove",value),stop(){++photoStops;state.playing=false;}},tasks:{onComplete(fn){complete=fn;},cancel(value){requests.push(["cancel",value]);}},
  timers:{setInterval(fn){intervals.push(fn);}},openApp(value){launches.push(value);return true;},navigation:{onChange(fn){navigate=fn;},push(value){navigate({page:value});},back(){navigate({page:"album"});}}};
 const context=vm.createContext({Ouse:api});vm.runInContext(fs.readFileSync(path.join(__dirname,"../apps",name,"app.js"),"utf8"),context);
 return {state,texts,styles,enabled,shown,requests,launches,scroll,click:key=>handlers[key](),tick:()=>intervals.forEach(fn=>fn()),
  finish(ok=true){const operation=requests.at(-1)[0];state.busy=false;state.error=ok?"ESP_OK":"ESP_FAIL";
   if(ok){if(operation==="start")state.running=true;if(operation==="record")state.recording=true;if(operation==="recordStop")state.recording=false;}
   complete({id,state:ok?"succeeded":"failed",error:state.error});},
  lifecycle:context.app,stopped:()=>stopped,photoStops:()=>photoStops};
}
{
 const app=load("camera",false);app.finish();app.click("shutter");assert(!app.requests.some(item=>item[0]==="capture"));assert(app.shown.has("dialog"));assert(app.texts.detail.includes("SD 卡"));
 app.state.ready=app.state.present=true;app.click("dismiss");app.click("shutter");assert.equal(app.requests.at(-1)[0],"capture");
 app.click("shutter");assert.equal(app.requests.filter(item=>item[0]==="capture").length,1);app.finish();assert.equal(app.requests.at(-1)[0],"list");
 app.state.entries=[{path:"Pictures/IMG_test.jpg"}];app.finish();assert(app.shown.has("albumThumb"));
 assert(!app.requests.some(item=>item[0]==="record"||item[0]==="recordStop"));
 assert(!app.shown.has("recordTime"));assert.equal(app.enabled.album,true);
 app.state.ready=app.state.present=true;app.click("album");assert.deepEqual(app.launches,["user.gallery"]);
 assert(!("status" in app.texts));app.lifecycle.onPause();app.lifecycle.onClose();assert.equal(app.stopped(),2);
}
{
 const app=load("camera",false);app.finish(false);assert(app.shown.has("dialog"));app.click("dismiss");app.tick();assert(!app.shown.has("dialog"));assert(!("status" in app.texts));
 const page=fs.readFileSync(path.join(__dirname,"../apps/camera/page.json"),"utf8");assert(!page.includes('"status"'));assert(!page.includes('"title"'));assert(!page.includes('"retry"'));
 assert(!page.includes('"mode"'));assert(!page.includes('"recordTime"'));assert(!page.includes("录像"));
 const source=fs.readFileSync(path.join(__dirname,"../apps/camera/app.js"),"utf8");assert(!source.includes("Ouse.camera.record"));
 const controls=JSON.parse(page).pages[0].root.children.find(widget=>widget.id==="controls");
 assert.equal(controls.children[0].width,controls.children.at(-1).width);
 assert.equal(controls.children.find(widget=>widget.id==="shutter").children[0].style.radius,27);
}
{
 const app=load("camera",false);app.finish();app.state.ready=app.state.present=true;app.click("shutter");app.finish(false);assert(app.shown.has("dialog"));assert(!app.state.recording);
}
{
 const app=load("camera",false);app.finish();app.state.asleep=true;app.state.running=false;
 const before=app.requests.length;app.tick();assert.equal(app.requests.length,before);
 app.state.asleep=false;app.tick();assert.equal(app.requests.at(-1)[0],"start");app.finish();
 app.lifecycle.onPause();app.tick();assert.equal(app.requests.at(-1)[0],"start");
}
{
 const app=load("gallery",false);assert.equal(app.requests.length,0);assert(app.shown.has("dialog"));
 app.state.ready=app.state.present=true;app.tick();assert.deepEqual(app.requests.at(-1),["list",0]);
 app.state.entries=[{path:"Pictures/IMG_test.jpg",bytes:42,image:"@thumb0",video:false},{path:"Pictures/VID_test.avi",bytes:9000,image:"@thumb1",video:true}];app.state.more=true;
 app.finish();app.click("tile0");assert.deepEqual(app.requests.at(-1),["open","Pictures/IMG_test.jpg"]);app.finish();
 assert.equal(app.enabled.viewerPrevious,false);assert.equal(app.enabled.viewerNext,true);assert(!app.shown.has("replay"));
 app.click("viewerNext");assert.deepEqual(app.requests.at(-1),["open","Pictures/VID_test.avi"]);app.state.playing=true;app.finish();assert(!app.shown.has("replay"));
 app.state.playing=false;app.tick();assert(app.shown.has("replay"));app.click("replay");app.finish();app.click("back");assert.deepEqual(app.requests.at(-1),["list",0]);app.finish();
 app.scroll.y=110;app.scroll.bottom=0;app.tick();assert.deepEqual(app.requests.at(-1),["list",3]);app.state.offset=3;app.finish();
 const afterAppend=app.requests.length;app.tick();assert.equal(app.requests.length,afterAppend);
 app.scroll.y=40;app.scroll.bottom=100;app.tick();app.scroll.y=10;app.tick();assert.deepEqual(app.requests.at(-1),["list",0]);app.state.offset=0;app.finish();
 app.click("tile0");app.finish();assert.equal(app.texts.takenAt,"拍摄时间未知");app.click("delete");assert(app.shown.has("deleteDialog"));assert.notEqual(app.requests.at(-1)[0],"remove");
 app.click("cancelDelete");assert(!app.shown.has("deleteDialog"));app.click("delete");app.click("confirmDelete");assert.deepEqual(app.requests.at(-1),["remove","Pictures/IMG_test.jpg"]);app.finish();assert.deepEqual(app.requests.at(-1),["list",0]);app.finish();
 app.state.ready=app.state.present=false;app.tick();assert(app.shown.has("dialog"));assert(app.texts.detail.includes("SD 卡"));
 app.lifecycle.onPause();app.lifecycle.onClose();assert(app.photoStops()>=3);
 const page=fs.readFileSync(path.join(__dirname,"../apps/gallery/page.json"),"utf8");assert(!page.includes('"title"'));assert(!page.includes('"refresh"'));assert(!page.includes('查看'));assert(!page.includes('"name"'));assert(page.includes('"text": "相册"'));assert(!page.includes('"paging"'));
}
console.log("PASS mobile camera/gallery: photo-only camera, SD gate, centered round shutter, thumbnail launch, direct tile preview, video replay, lifecycle, no headings/status/retry/view buttons");
