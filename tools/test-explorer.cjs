const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
function create(card={ready:true,detected:true,totalBytes:32000000000,freeBytes:31000000000}){
 const texts=new Map(),hidden=new Map(),enabled=new Map(),handlers=new Map(),jobs=new Map(),cancelled=[],images=new Map(),scroll={y:0,bottom:1000,height:340,pending:false};
 let timer,next=1,latest,backHandler,backEnabled=false;
 const api={
  ui:{setText(id,text){assert(Buffer.byteLength(text)<=256);texts.set(id,text);},setImage(id,asset){images.set(id,asset);},getScroll(){return {...scroll};},scrollTo(id,position){assert.equal(id,'entries');scroll.y=position;return true;},show(id){hidden.set(id,false);},hide(id){hidden.set(id,true);},setEnabled(id,value){enabled.set(id,value);},on(id,event,fn){handlers.set(id+':'+event,fn);}},
  files:{get(){return card;},browse(directory,offset){const id=next++;latest={id,directory,offset};jobs.set(id,{pending:true});return id;},directory(id){return jobs.get(id);}},
  navigation:{onBack(fn){backHandler=fn;},setBackEnabled(value){backEnabled=value;}},
  tasks:{cancel(id){cancelled.push(id);return true;}},timers:{setInterval(fn){timer=fn;}}
 };
 const context=vm.createContext({Ouse:api});vm.runInContext(fs.readFileSync(path.join(__dirname,'../apps/explorer/app.js'),'utf8'),context,{timeout:1000});
 return {api,context,texts,hidden,enabled,jobs,cancelled,card,images,scroll,back(){if(hidden.get('dialog')===false){hidden.set('dialog',true);return true;}if(!backEnabled)return false;backHandler();return true;},frame(){timer();},edge(position,bottom){scroll.y=position;scroll.bottom=bottom;timer();timer();timer();},request(){return latest;},click(id){assert.notEqual(enabled.get(id),false);handlers.get(id+':click')();},finish(entries=[],more=false,error='ESP_OK'){jobs.set(latest.id,{pending:false,entries,more,error});timer();}};
}
const empty=create({ready:false,detected:false});assert.equal(empty.hidden.get('dialog'),false);assert.equal(empty.texts.get('dialog'),'未插入 SD 卡');assert.equal(empty.request(),undefined);
empty.click('dismiss');empty.frame();assert.equal(empty.hidden.get('dialog'),true);empty.context.app.onResume();assert.equal(empty.hidden.get('dialog'),false);
const damaged=create({ready:false,detected:true});assert.equal(damaged.texts.get('dialog'),'无法读取 SD 卡');
const removedInFolder=create();removedInFolder.finish([{name:'Folder',path:'/Folder',directory:true,accessible:true}]);removedInFolder.click('entry0');removedInFolder.finish([]);removedInFolder.card.ready=false;removedInFolder.card.detected=false;removedInFolder.frame();assert(removedInFolder.back());assert.equal(removedInFolder.back(),false,'Missing card must not trap back in a stale folder');
const browser=create();assert.equal(browser.request().directory,'/');assert.equal(browser.enabled.get('refresh'),false);
const root=[{name:'Outside-Ouse',path:'/Outside-Ouse',directory:true,accessible:true},{name:'.hidden.txt',path:'/.hidden.txt',bytes:123,directory:false,accessible:true}];
browser.finish(root,false);assert.equal(browser.texts.get('entry1'),'.hidden.txt');assert.equal(browser.enabled.get('up'),false);assert.equal(browser.images.get('icon0'),'folder');assert.equal(browser.images.get('icon1'),'document');assert.equal(browser.hidden.get('status'),true);assert.equal(browser.texts.has('path'),false);assert.equal(browser.texts.has('page'),false);browser.click('info');assert.equal(browser.texts.get('dialog'),'存储信息');browser.click('dismiss');
browser.click('entry0');assert.equal(browser.request().directory,'/Outside-Ouse');browser.finish([{name:'song.wav',path:'/Outside-Ouse/song.wav',bytes:480044,accessible:true,directory:false}]);
browser.click('entry0');assert.equal(browser.texts.get('dialog'),'文件详情');assert(browser.texts.get('detail').includes('468.8 KiB'));assert(browser.back());assert.equal(browser.texts.get('title'),'Outside-Ouse');assert(browser.back());assert.equal(browser.request().directory,'/');assert(browser.back(),'Loading must consume back without exiting');browser.finish(root,true);assert.equal(browser.back(),false,'Root must return control to system');
browser.click('entry0');browser.finish([],false,'ESP_FAIL');assert.equal(browser.texts.get('title'),'SD 卡');assert.equal(browser.texts.get('dialog'),'读取失败');browser.click('dismiss');
browser.click('refresh');const pending=browser.request().id;browser.card.ready=false;browser.card.detected=false;browser.frame();assert(browser.cancelled.includes(pending));assert.equal(browser.hidden.get('row0'),true);assert.equal(browser.texts.get('dialog'),'未插入 SD 卡');
browser.jobs.set(pending,{pending:false,error:'ESP_OK',entries:root,more:true});browser.frame();assert.equal(browser.hidden.get('row0'),true);
browser.card.ready=true;browser.context.app.onResume();assert.equal(browser.request().directory,'/');browser.finish([{name:'中文'.repeat(80)+'.txt',path:'/中文'.repeat(70),bytes:0,accessible:true,directory:false}]);browser.click('entry0');assert(Buffer.byteLength(browser.texts.get('detail'))<=256);
const rolling=create();const items=(offset,count=8)=>Array.from({length:count},(_,index)=>({name:'file'+(offset+index),path:'/file'+(offset+index),bytes:1,accessible:true,directory:false}));
rolling.finish(items(0),true);rolling.edge(100,0);assert.equal(rolling.request().offset,8);rolling.finish(items(8),true);
rolling.edge(550,0);assert.equal(rolling.request().offset,16);rolling.finish(items(16),true);
rolling.edge(1000,0);assert.equal(rolling.request().offset,24);rolling.finish(items(24,5),false);assert.equal(rolling.texts.get('entry0'),'file8');assert.equal(rolling.scroll.y,552);assert.equal(rolling.hidden.get('row21'),true);
rolling.edge(0,900);assert.equal(rolling.request().offset,0);rolling.finish(items(0),true);assert.equal(rolling.texts.get('entry0'),'file0');assert.equal(rolling.texts.get('entry23'),'file23');assert.equal(rolling.scroll.y,448);
rolling.edge(1000,0);assert.equal(rolling.request().offset,24);rolling.finish(items(24,5),false);assert.equal(rolling.texts.get('entry0'),'file8');assert.equal(rolling.texts.get('entry20'),'file28');
rolling.click('refresh');assert.equal(rolling.request().offset,0);rolling.finish([],false);assert.equal(rolling.scroll.y,0);assert.equal(rolling.texts.get('status'),'此文件夹为空');
console.log('PASS explorer: continuous lazy scroll, bounded bidirectional windows, partial final batch, clean labels/icons, storage popup, errors and no-card handling');
