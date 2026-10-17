// Execute the shipped application scripts against a strict mounted-page API.
// Native I/O is covered separately; this checks application state transitions.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const assert=require('node:assert/strict');
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function app(name,saved){
 const base=path.join(__dirname,'../apps',name),doc=JSON.parse(fs.readFileSync(path.join(base,'page.json'),'utf8'));
 let page=doc.initialPage,nodes=new Map(),handlers=new Map(),nav,complete,interval,lease=false,media=null,next=10,alarms=[],operations=[];
 const values=new Map(),texts=new Map(),enabled=new Map(),hidden=new Map(),tasks=new Map(),store=new Map();if(saved)store.set('library',saved);
 const networkReplies=[];
 function mount(){nodes.clear();handlers.clear();values.clear();texts.clear();enabled.clear();hidden.clear();function walk(n){assert(!nodes.has(n.id));nodes.set(n.id,n);if(n.value!==undefined)values.set(n.id,n.value);if(n.text!==undefined)texts.set(n.id,n.text);if(n.options)assert(n.options.split('\n').length<=24);for(const c of n.children||[])walk(c);}walk(doc.pages.find(p=>p.id===page).root);}
 function node(id){assert(nodes.has(id),'Widget not mounted: '+page+'/'+id);return nodes.get(id);}
 function command(operation,data){const id=next++;operations.push({id,operation,data});return id;}
 const api={
  system:{health(){return {phase:'running',services:[{name:'storage',state:'ready'}]};},activities(){return [];},capabilities(){return {network:{available:true,responseBodyMax:32768},alarms:{available:true,perAppMax:4}};}},
  usb:{get(){return {available:true,mounted:false};}},
  files:{get(){return {ready:true,count:1,revision:1};},list(){return [{path:'Music/fur-elise-30s.wav',bytes:480044}];},refresh(){return true;},download(options){return command('download',options);}},
  net:{request(){const promise=new Promise(resolve=>networkReplies.push(resolve));promise.taskId=next++;return promise;},reply(response){networkReplies.shift()(response);}},
  ui:{setText(id,v){node(id);assert(Buffer.byteLength(String(v))<=256);texts.set(id,String(v));},getText(id){node(id);return texts.get(id)||'';},setValue(id,v){node(id);values.set(id,v);},getValue(id){node(id);return values.get(id)||0;},setEnabled(id,v){node(id);enabled.set(id,v);},show(id){assert.equal(arguments.length,1);node(id);hidden.set(id,false);},hide(id){node(id);hidden.set(id,true);},on(id,event,fn){node(id);handlers.set(id+':'+event,fn);}},
  navigation:{push(id){const old=page;page=id;mount();nav({page,previous:old});return true;},back(){const old=page;page=doc.initialPage;mount();nav({page,previous:old});return true;},onChange(fn){nav=fn;}},
  storage:{get(key){return Promise.resolve(store.get(key));},set(key,value){store.set(key,JSON.parse(JSON.stringify(value)));return Promise.resolve();}},
  background:{begin(){lease=true;return true;},end(){lease=false;},isActive(){return lease;}},
  media:{get(){return media?{...media}:null;},play(options){assert(!media||!media.active);const id=next++;media={taskId:id,active:true,paused:false,interrupted:false,positionMs:0,durationMs:30000,errorCode:0,state:'playing'};operations.push({operation:'play',options});return id;},pause(){if(!media||!media.active)return false;media.paused=true;return true;},resume(){media.paused=false;return true;},stop(){if(!media||!media.active)return false;media.active=false;tasks.set(media.taskId,{state:'cancelled'});return true;}},
  tasks:{get(id){return tasks.get(id);},onComplete(fn){complete=fn;}},notifications:{onOpen(){}},timers:{setInterval(fn){interval=fn;return 1;}},clock:{get(){return {valid:true,utcMs:Date.UTC(2026,9,17,0,0),syncedThisBoot:true};}},
  alarms:{list(){return alarms.map(a=>({...a}));},set(options){return command('set',{...options});},remove(id){return command('remove',id);},setEnabled(id,value){assert.equal(typeof value,'boolean');return command('enabled',{id,value});},dismiss(id){return command('dismiss',id);},snooze(id,minutes){return command('snooze',{id,minutes});}}
 };
 mount();const context=vm.createContext({Ouse:api,console});vm.runInContext(fs.readFileSync(path.join(base,'app.js'),'utf8'),context,{timeout:1000});
 return {context,api,store,texts,enabled,hidden,operations,click(id,event='click'){assert.notEqual(enabled.get(id),false,'Disabled button '+id);node(id);assert(handlers.has(id+':'+event),id);handlers.get(id+':'+event)();},frame(){interval();},lease(){return lease;},revoke(){lease=false;},media(){return media;},finish(error=0){media.active=false;media.errorCode=error;media.error=error?'ESP_FAIL':'ESP_OK';tasks.set(media.taskId,{state:error?'failed':'succeeded'});complete({id:media.taskId,state:error?'failed':'succeeded'});},complete(error=false){const op=operations.at(-1);if(!error){if(op.operation==='set'){const old=alarms.findIndex(a=>a.id===op.data.id);const a={...op.data,enabled:true,ringing:false,nextMs:op.data.atMs};if(old<0)alarms.push(a);else alarms[old]=a;}if(op.operation==='enabled')alarms.find(a=>a.id===op.data.id).enabled=op.data.value;if(op.operation==='remove')alarms=alarms.filter(a=>a.id!==op.data);}complete({id:op.id,state:error?'failed':'succeeded',error:error?'ESP_FAIL':'ESP_OK'});},alarmList(){return alarms;}};
}
(async()=>{
 const service=app('service-test');
 for(const category of ['overview','usb','alarms','media']){service.click('go_'+category);service.click('back');}
 service.click('go_network');service.api.ui.setText('url','https://example.test/');service.click('url','change');service.click('get');service.click('back');
 service.click('go_media');assert.equal(service.texts.get('url'),'https://example.test/');service.click('back');
 service.click('go_network');assert(service.texts.get('network').includes('请求'));service.click('back');
 service.api.net.reply({status:200,bodyBytes:1000,body:'测试'.repeat(300)});await tick();service.click('go_network');assert(service.texts.get('network').includes('HTTP 200'));service.click('back');
 assert.notDeepEqual(fs.readFileSync(path.join(__dirname,'../apps/countdown/icon.bin')),fs.readFileSync(path.join(__dirname,'../apps/stopwatch/icon.bin')));
 const m=app('music');await tick();assert.equal(m.texts.get('track'),'致爱丽丝 · 30 秒');m.click('play');assert.equal(m.operations.at(-1).options.path,'Music/fur-elise-30s.wav');assert(m.lease());m.click('pause');assert(m.media().paused&&!m.lease());m.click('pause');assert(!m.media().paused&&m.lease());m.click('stop');m.frame();assert(!m.lease());
 m.click('library');m.api.ui.setText('name','局域网测试');m.api.ui.setText('url','http://192.0.2.1/song.wav');m.click('add');await tick();assert.equal(m.store.get('library').tracks.length,1);m.click('back');m.click('mode');m.click('play');m.finish();assert.equal(m.operations.at(-1).options.url,'http://192.0.2.1/song.wav');m.finish();assert(!m.lease());
 m.click('previous');m.click('mode');m.finish();assert(m.media().active&&m.lease());m.revoke();m.finish();assert(!m.media().active&&!m.lease(),'Revoked background must not restart playback');
 m.click('play');m.finish(1);assert(!m.media().active&&!m.lease(),'Failed track must not loop');
 m.click('play');m.context.app.onClose();assert(m.media().active&&!m.lease(),'Closing JS must preserve native current track');
 const restored=app('music',m.store.get('library'));await tick();restored.click('library');assert.equal(restored.texts.get('item1'),'局域网测试');
 restored.api.ui.setText('name','sd-download');restored.api.ui.setText('url','https://example.test/music.wav');restored.click('download');assert.equal(restored.operations.at(-1).data.path,'Music/sd-download.wav');restored.complete();assert(restored.texts.get('listStatus').includes('完成'));
 const card=app('music');await tick();card.api.files.get=()=>({ready:false,count:0,revision:2});card.frame();assert.equal(card.enabled.get('play'),false);assert.equal(card.texts.get('track'),'没有可播放的音乐');
 card.api.files.get=()=>({ready:true,count:20,revision:3});card.api.files.list=(offset,limit)=>Array.from({length:Math.min(limit,20-offset)},(_,index)=>({path:'Music/track-'+(offset+index)+'.wav',bytes:100}));card.frame();card.click('library');card.click('list_next');assert.equal(card.texts.get('item0'),'track-8');card.click('back');
 const a=app('alarm');a.click('add');assert.equal(a.texts.get('heading'),'新建闹钟');a.api.ui.setText('name','上班');a.api.ui.setValue('d1',true);a.click('save');assert.equal(a.operations.at(-1).data.weekdays,2);assert.equal(a.enabled.get('save'),false);a.complete();a.frame();assert(a.texts.get('edit0').includes('上班'));
 a.click('enable0','change');a.complete();a.frame();assert.equal(a.alarmList()[0].enabled,false);a.click('enable0','change');a.complete();assert(a.alarmList()[0].enabled);
 a.click('edit0');a.api.ui.setText('name','编辑后');a.click('save');a.complete(true);assert(a.texts.get('status').includes('失败'));assert(a.enabled.get('save'));a.click('save');a.complete();a.frame();assert(a.texts.get('edit0').includes('编辑后'));
 a.click('edit0');a.click('remove');a.complete();a.frame();assert.equal(a.alarmList().length,0);
 console.log('PASS service categories, late network replies, text budgets, distinct timer icons; music/alarm lifecycle and failure retry');
})().catch(error=>{console.error(error);process.exitCode=1;});
