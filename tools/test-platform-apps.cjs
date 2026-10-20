'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
function load(folder,files=new Map()){
 const page=JSON.parse(fs.readFileSync(folder+'/page.json','utf8')),nodes=new Map();function walk(n){nodes.set(n.id,n);for(const child of n.children||[])walk(child);}walk(page.root);
 const texts=new Map(),frames=new Map(),actors=new Map(),visible=new Map(),handlers=new Map(),timers=new Map();let clock=Date.UTC(2026,9,20),next=1,change;
 const display={contentWidthDp:320,contentHeightDp:416,orientation:'portrait'};
 const ui={layout(batch){assert(batch.length<=64);for(const f of batch){assert(nodes.has(f.id),f.id);for(const k of ['x','y','width','height'])assert(Number.isInteger(f[k]),JSON.stringify(f));assert(f.width>0&&f.height>0);frames.set(f.id,{...f});}return true;},on(id,event,fn){assert(nodes.has(id),id);handlers.set(id+':'+event,fn);},setText(id,t){assert(nodes.has(id));texts.set(id,t);},getText(id){return texts.get(id)||'';},setStyle(id,s){assert(nodes.has(id));},setEnabled(id,b){assert(nodes.has(id));},show(id){assert(nodes.has(id));visible.set(id,true);},hide(id){assert(nodes.has(id));visible.set(id,false);}};
 class Clock extends Date{constructor(...args){super(...(args.length?args:[clock]));}static now(){return clock;}}
 const storage=new Map();const api={ui,display:{get:()=>display,onChange(fn){change=fn;},setOrientation(value){display.orientation=value;display.contentWidthDp=value==='landscape'?480:320;display.contentHeightDp=value==='landscape'?256:416;change();return true;}},sprites:{update(batch){assert(batch.length<=32);for(const p of batch){assert(nodes.has(p.id));actors.set(p.id,{...actors.get(p.id),...p});}}},storage:{get:k=>Promise.resolve(storage.get(k)),set(k,v){storage.set(k,v);return Promise.resolve(true);},remove(k){storage.delete(k);return Promise.resolve(true);}},sd:{read(path){return files.has(path)?Promise.resolve(files.get(path).slice(0)):Promise.reject(new Error('NOT_FOUND'));},write(path,data){files.set(path,data.slice(0));return Promise.resolve(data.byteLength);}},timers:{setInterval(fn){const id=next++;timers.set(id,fn);return id;},clear(id){timers.delete(id);}}};
 const context=vm.createContext({Ouse:api,Date:Clock,Uint8Array,ArrayBuffer,console});vm.runInContext(fs.readFileSync(folder+'/app.js','utf8'),context);
 return {api,context,display,nodes,frames,actors,texts,files,storage,visible,timers,swipe(id,direction){handlers.get(id+':swipe')(direction);},click(id){const fn=handlers.get(id+':click');assert(fn,id);fn();},pointer(id,x,y){handlers.get(id+':pointerdown')({x,y});},tick(n=1){for(let i=0;i<n;i++){clock+=50;for(const fn of [...timers.values()])fn();}},land(){api.display.setOrientation('landscape');},portrait(){api.display.setOrientation('portrait');}};
}
(async()=>{
 const puzzle=load('apps/g2048');await new Promise(setImmediate);
 vm.runInContext('grid=[2,2,4,4,0,0,0,0,0,0,0,0,0,0,0,0];score=0;over=false;paint()',puzzle.context);
 puzzle.swipe('c0','left');assert.equal(puzzle.texts.get('score'),'分 12');
 const progress=vm.runInContext('JSON.stringify({grid,score,best})',puzzle.context);
 for(const [w,h] of [[320,416],[480,256],[480,240],[320,400]]){
  puzzle.display.contentWidthDp=w;puzzle.display.contentHeightDp=h;vm.runInContext('layout()',puzzle.context);
  assert.equal(vm.runInContext('JSON.stringify({grid,score,best})',puzzle.context),progress);
  const board=puzzle.frames.get('board');assert.equal(board.width,board.height);assert(board.width>=200);
  for(const [id,f] of puzzle.frames){const parent=id.startsWith('c')?puzzle.frames.get('r'+Math.floor(Number(id.slice(1))/4)):id.match(/^r[0-3]$/)?board:{width:w,height:h};assert(f.x>=0&&f.y>=0&&f.x+f.width<=parent.width&&f.y+f.height<=parent.height,JSON.stringify({id,f,parent}));if(id.match(/^c\d+$/))assert.equal(f.width,f.height);}
 }
 puzzle.click('restart');assert.equal(puzzle.texts.get('score'),'分 0');assert.equal(puzzle.texts.get('best'),'最佳 12');
 const calculator=load('apps/calculator');await new Promise(setImmediate);
 calculator.click('b_2');calculator.click('b_add');calculator.click('b_3');calculator.click('b_eq');assert.equal(calculator.texts.get('result'),'5');
 for(const mode of ['simple','science','prog'])for(const [w,h] of [[320,416],[480,256],[480,240],[320,400]]){
  calculator.display.contentWidthDp=w;calculator.display.contentHeightDp=h;calculator.click('pick_'+mode);
  const pad=calculator.frames.get('pad');assert(pad.height>=212);assert(pad.x+pad.width<=w);if(w>h)assert(pad.y+pad.height<=h);
  for(const id of ['science','trig','prog','hex'])assert(calculator.frames.get(id).width>=160);
 }
 const c=load('apps/calendar');await Promise.resolve();assert.equal(c.texts.get('month'),'2026 / 10');
 for(const [w,h] of [[320,416],[480,256],[480,240],[320,400]]){c.display.contentWidthDp=w;c.display.contentHeightDp=h;c.api.display.setOrientation(w>h?'landscape':'portrait');c.display.contentWidthDp=w;c.display.contentHeightDp=h;vm.runInContext('layout()',c.context);for(const [id,f] of c.frames){assert(f.x>=0&&f.y>=0&&f.x+f.width<=w&&f.y+f.height<=h,JSON.stringify({id,f,w,h}));}}
 c.click('next');assert.equal(c.texts.get('month'),'2026 / 11');c.click('previous');c.click('today');c.click('edit');c.texts.set('note','测试记事');c.click('save');await new Promise(setImmediate);assert.equal(c.storage.get('day.20261020'),'测试记事');assert.equal(c.texts.get('summary'),'测试记事');assert.equal(c.visible.get('editor'),false);
 const game=load('apps/pocket-adventure');await new Promise(setImmediate);assert.equal(game.timers.size,1);game.land();assert.equal(game.frames.get('field').width,464);
 const coin=game.actors.get('coin0');game.pointer('field',coin.x+12,coin.y+16);game.tick(80);assert.equal(game.visible.get('coin0'),false);assert.equal(game.texts.get('score'),'1 / 3');game.click('save');await new Promise(setImmediate);assert(game.files.has('save.json'));
 game.context.app.onPause();assert.equal(game.timers.size,0);game.context.app.onResume();assert.equal(game.timers.size,1);game.portrait();game.context.app.onClose();assert.equal(game.timers.size,0);
 const restored=load('apps/pocket-adventure',game.files);await new Promise(setImmediate);assert.equal(restored.texts.get('score'),'1 / 3');assert.equal(restored.visible.get('coin0'),false);restored.context.app.onClose();
 console.log('PASS real app scripts: 2048 square cells, merge/score and rotation persistence; calculator modes, calculation and usable key layout; calendar notes; game movement/SD/lifecycle across four viewports');
})().catch(e=>{console.error(e);process.exitCode=1;});
