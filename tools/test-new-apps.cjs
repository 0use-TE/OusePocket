// Offline smoke for notepad / dice / weather / calculator BPK scripts.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const tick=()=>new Promise(r=>setImmediate(r));
function app(name,saved){
 const base=path.join(__dirname,'../apps',name),doc=JSON.parse(fs.readFileSync(path.join(base,'page.json'),'utf8'));
 let page=doc.initialPage||null,handlers=new Map(),nav,texts=new Map(),values=new Map(),enabled=new Map(),hidden=new Map(),store=new Map(),networkQueue=[];
 if(saved)Object.entries(saved).forEach(([k,v])=>store.set(k,v));
 const nodes=new Map();
 function walk(n){assert(!nodes.has(n.id));nodes.set(n.id,n);if(n.text!==undefined)texts.set(n.id,n.text);if(n.value!==undefined)values.set(n.id,n.value);for(const c of n.children||[])walk(c);}
 function mount(){nodes.clear();handlers.clear();texts.clear();values.clear();const root=doc.pages?doc.pages.find(p=>p.id===page).root:doc.root;walk(root);}
 function node(id){assert(nodes.has(id),'missing '+page+'/'+id);return nodes.get(id);}
 let taskSeq=7;
 const api={
  ui:{
   setText(id,v){node(id);texts.set(id,String(v));},getText(id){node(id);return texts.get(id)||'';},
   setValue(id,v){node(id);values.set(id,v);},getValue(id){node(id);return values.has(id)?values.get(id):0;},
   setEnabled(id,v){node(id);enabled.set(id,v);},setStyle(id){node(id);},show(id){node(id);hidden.set(id,false);},hide(id){node(id);hidden.set(id,true);},
   on(id,event,fn){node(id);handlers.set(id+':'+event,fn);}
  },
  navigation:{
   push(id){const old=page;page=id;mount();nav({page,previous:old});return true;},
   back(){const old=page;page=doc.initialPage;mount();nav({page,previous:old});return true;},
   onChange(fn){nav=fn;}
  },
  storage:{get(key){return Promise.resolve(store.get(key));},set(key,value){store.set(key,JSON.parse(JSON.stringify(value)));return Promise.resolve(true);}},
  clock:{get(){return {utcMs:Date.UTC(2026,9,18,2,0),valid:true};}},
  net:{request(){const promise=new Promise((resolve,reject)=>{networkQueue.push({resolve,reject});});promise.taskId=taskSeq++;return promise;}},
  tasks:{cancel(){}},
  timers:{setInterval(){return 1;},clear(){}}
 };
 if(doc.pages){page=doc.initialPage;mount();}else mount();
 const context=vm.createContext({Ouse:api,Math,JSON,console,encodeURIComponent});
 vm.runInContext(fs.readFileSync(path.join(base,'app.js'),'utf8'),context,{timeout:1000});
 return {
  texts,store,enabled,hidden,api,
  click(id,event='click'){assert(handlers.has(id+':'+event),id+':'+event);handlers.get(id+':'+event)();},
  change(id,value){if(value!==undefined)values.set(id,value);assert(handlers.has(id+':change'),id+':change');handlers.get(id+':change')(value);},
  reply(body){const item=networkQueue.shift();assert(item);item.resolve({ok:true,status:200,body});}
 };
}
(async()=>{
 const notes=app('notepad',{notes:{version:1,items:[{title:'买菜',body:'牛奶鸡蛋',updated:1}]}});
 await tick();
 assert(notes.texts.get('item0').includes('买菜'));

 const dice=app('dice');await tick();
 dice.click('roll');
 assert.match(dice.texts.get('die0'),/^[1-6]$/);

 const weather=app('weather');await tick();
 weather.reply(JSON.stringify({status:'success',city:'Hangzhou',lat:30.27,lon:120.15}));
 await tick();
 weather.reply(JSON.stringify({current:{temperature_2m:22.6,relative_humidity_2m:55,weather_code:0,wind_speed_10m:8.2}}));
 await tick();
 assert.equal(weather.texts.get('temp'),'23°');
 assert.equal(weather.texts.get('place'),'自动 · Hangzhou');
 weather.api.ui.setText('query','昆山');
 weather.click('search');
 weather.reply(JSON.stringify({results:[{name:'昆山',latitude:31.38,longitude:120.95,admin1:'江苏省',admin2:'苏州市'}]}));
 await tick();
 assert.equal(weather.texts.get('hit0'),'昆山 · 苏州市 · 江苏省');
 weather.click('hit0');
 weather.reply(JSON.stringify({current:{temperature_2m:18.2,relative_humidity_2m:70,weather_code:3,wind_speed_10m:12}}));
 await tick();
 assert.equal(weather.texts.get('place'),'昆山');
 assert.equal(weather.texts.get('temp'),'18°');

 const calc=app('calculator');await tick();
 calc.click('b_1');calc.click('b_add');calc.click('b_2');calc.click('b_eq');
 assert.equal(calc.texts.get('result'),'3');
 calc.click('b_ac');calc.click('modeBtn');calc.click('pick_science');calc.click('b_9');calc.click('b_sqrt');
 assert.equal(calc.texts.get('result'),'3');
 assert.equal(calc.texts.get('modeBtn'),'科学');
 calc.click('modeBtn');calc.click('pick_prog');calc.click('b_base');
 assert.equal(calc.texts.get('modeBtn'),'程序');
 assert.equal(calc.texts.get('b_base'),'HEX');
 assert.equal(calc.store.get('calculator').mode,'prog');

 console.log('PASS notepad/dice/weather/calculator smoke');
})().catch(error=>{console.error(error);process.exitCode=1;});
