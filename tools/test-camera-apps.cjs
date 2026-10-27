const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
function load(name,ready,permission="granted"){
 const state={ready,present:ready,running:false,busy:false,inUse:false,error:"ESP_OK",recording:false,recordedMs:0,playing:false,entries:[],offset:0,more:false,path:""};
 const texts={},styles={},enabled={},shown=new Set(),handlers={},intervals=[],requests=[],launches=[],scroll={y:0,bottom:200,height:300,pending:false};let complete,navigate,id=100,stopped=0,photoStops=0;
 const schema=JSON.parse(fs.readFileSync(path.join(__dirname,"../apps",name,"page.json"),"utf8")),pages=schema.pages||[{id:"root",root:schema.root}];let current=pages[0];const ids=()=>{const out=new Set();function walk(n){out.add(n.id);for(const c of n.children||[])walk(c);}walk(current.root);return out;};const valid=id=>assert(ids().has(id),`unknown current-page widget ${name}/${current.id}/${id}`);
 const ui={setImage(key,value){styles[key]={src:value};},layout(items){for(const item of items){valid(item.id);for(const k of ["x","y","width","height"])assert(Number.isInteger(item[k]),item.id+" "+k);assert(item.width>0&&item.height>0);styles[item.id]={...styles[item.id],...item};}},setText(key,value){texts[key]=value;},setStyle(key,value){styles[key]=value;},setEnabled(key,value){enabled[key]=value;},show(key){valid(key);shown.add(key);},hide(key){valid(key);shown.delete(key);},on(key,event,fn){valid(key);handlers[key]=fn;},getScroll(){return scroll;},scrollTo(key,value){scroll.y=value;}};
 const request=(operation,value)=>{requests.push([operation,value]);if(state.inUse&&operation==="start")return false;state.busy=true;return ++id;};
 const permissionValues={camera:permission,sharedDelete:permission},settings=[];
 let displayChange;const display={contentWidthDp:320,contentHeightDp:416};
 const api={display:{get:()=>display,setFullscreen(value){display.fullscreen=value;},onChange(fn){displayChange=fn;}},ui,keys:{on(){}},permissions:{get:key=>permissionValues[key],request:key=>permissionValues[key]},system:{openSettings:key=>settings.push(key)},power:{get:()=>state},files:{get:()=>state,onOpen(){}},camera:{get:()=>state,start:()=>request("start"),capture:()=>request("capture"),record:()=>request("record"),recordStop:()=>request("recordStop"),stop(){++stopped;state.running=false;state.recording=false;}},
  photos:{get:()=>state,list:offset=>request("list",offset),open:value=>request("open",value),remove:value=>request("remove",value),stop(){++photoStops;state.playing=false;}},tasks:{onComplete(fn){complete=fn;},cancel(value){requests.push(["cancel",value]);}},
  timers:{setInterval(fn){intervals.push(fn);}},openApp(value){launches.push(value);return true;},navigation:{onChange(fn){navigate=fn;},push(value){current=pages.find(p=>p.id===value);navigate({page:value});},back(){current=pages.find(p=>p.id==="album");navigate({page:"album"});}}};
 const context=vm.createContext({Ouse:api});vm.runInContext(fs.readFileSync(path.join(__dirname,"../apps",name,"app.js"),"utf8"),context);
 return {display,resize(){displayChange();},permissionValues,settings,state,texts,styles,enabled,shown,requests,launches,scroll,click:key=>handlers[key](),tick:()=>intervals.forEach(fn=>fn()),
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
 assert.equal(controls.children.length,2);
 assert.equal(app.display.fullscreen,true);assert.equal(app.styles.controls.y,320);
 function bounds(fill){const p=app.styles.preview,w=app.display.contentWidthDp,h=app.display.contentHeightDp;
  assert.equal(p.width*3,p.height*4,'preview keeps sensor ratio');
  assert(Math.abs(p.x-(w-p.width)/2)<=1&&Math.abs(p.y-(h-p.height)/2)<=1,'preview stays centered');
  if(fill){assert(p.x<=0&&p.y<=0&&p.x+p.width>=w&&p.y+p.height>=h,'preview covers viewport');}
  else {assert(p.x>=0&&p.y>=0&&p.x+p.width<=w&&p.y+p.height<=h,'whole image fits viewport');}
 }
 bounds(true);app.click('previewMode');bounds(false);assert.equal(app.texts.previewMode,'铺满取景');
 app.click('previewMode');bounds(true);assert.equal(app.texts.previewMode,'完整画面');
 app.display.contentWidthDp=480;app.display.contentHeightDp=256;app.resize();
 assert.equal(app.styles.controls.x,384);assert.equal(app.styles.controls.width,96);assert.equal(app.styles.shutter.y,90);
 bounds(true);app.click('previewMode');bounds(false);app.click('previewMode');bounds(true);
 for(const [w,h] of [[320,440],[480,280],[320,480],[480,320]]){app.display.contentWidthDp=w;app.display.contentHeightDp=h;app.resize();bounds(true);app.click('previewMode');bounds(false);app.click('previewMode');}
 app.lifecycle.onPause();app.display.fullscreen=false;app.lifecycle.onResume();assert.equal(app.display.fullscreen,true);bounds(true);
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
{
 const app=load("camera",true,"denied");assert(app.shown.has("permissionSettings"));assert.equal(app.requests.length,0);for(let i=0;i<10;i++)app.tick();assert.equal(app.requests.length,0);app.click("permissionSettings");assert.equal(app.settings.at(-1),"permissions");app.permissionValues.camera="granted";app.lifecycle.onResume();assert.equal(app.requests.at(-1)[0],"start");
}
{
 const app=load("gallery",true,"denied");app.state.entries=[{path:"Pictures/IMG_test.jpg",bytes:42,image:"@thumb0",video:false}];app.finish();app.click("tile0");app.finish();app.click("delete");app.click("confirmDelete");assert(app.shown.has("viewerPermissionSettings"));assert(!app.requests.some(item=>item[0]==="remove"));app.click("viewerPermissionSettings");assert.equal(app.settings.at(-1),"permissions");
}
{
 const app=load("camera",false);app.finish();app.state.ready=app.state.present=true;
 for(const stage of ["camera","encode","storage"]){app.click("shutter");app.state.errorStage=stage;app.finish(false);assert(app.shown.has("dialog"));assert.equal(app.texts.detail.includes("SD 卡"),stage==="storage");app.click("dismiss");}
}
{
 const app=load("gallery",true);app.state.entries=[{path:"Pictures/a.jpg",image:"@thumb0"}];app.state.more=true;app.finish();
 app.scroll.y=110;app.scroll.bottom=0;app.tick();assert.equal(app.enabled.tile0,true);assert.equal(app.styles.thumb0.src,"@thumb0");
 app.scroll.y=130;app.state.offset=3;app.state.entries=[{path:"Pictures/b.jpg",image:"@thumb12"}];app.finish();
 assert.equal(app.styles.thumb0.src,"@thumb12");assert.equal(app.scroll.y,24);
}
console.log("PASS camera/gallery: portrait/landscape overlays, error stages, SD gate, stable thumbnail sources without dimming, scroll anchoring, video/delete and lifecycle");
