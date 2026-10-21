'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),path=require('path');
const folder=path.resolve(__dirname,'../apps/survivor'),source=n=>fs.readFileSync(path.join(folder,n),'utf8');
const ec=vm.createContext({console});vm.runInContext(source('engine.js'),ec);
const create=vm.runInContext('createSurvivorEngine',ec),advance=(e,t)=>{for(let i=0;i<Math.ceil(t*60);i++)e.advance(1/60);};
for(let gun=0;gun<6;gun++){
 const e=create(1);e.reset({gun});e.state.spawn=999;const f=e.spawn(0);Object.assign(f,{x:e.state.hero.x+75,y:e.state.hero.y,hp:15,speed:0});advance(e,2);assert(!f.on,'real weapon damage '+gun);
}
for(let hero=0;hero<4;hero++){
 const e=create();e.reset({hero});const s=e.state;s.spawn=999;assert(e.skill());assert(!e.skill());const x=s.hero.x;advance(e,.1);
 if(hero===0||hero===3)assert(s.hero.x>x+20);if(hero===1){const hp=s.hero.hp;e.hurt(20);assert(hp-s.hero.hp<4);}if(hero===2)assert(s.turret.time>7);
}
for(let pet=0;pet<4;pet++){
 const e=create();e.reset({pet});const s=e.state;s.spawn=999;s.fire=999;
 if(pet===0||pet===3){const f=e.spawn(0);Object.assign(f,{x:s.hero.x+25,y:s.hero.y,speed:0,hp:500});advance(e,2);assert(f.hp<500,'pet damage');}
 if(pet===1){e.drop(s.hero.x+120,s.hero.y,2);advance(e,1);assert(!s.gems[0].on&&s.xp===2,'cat collects distant XP');}
 if(pet===2){s.hero.hp-=40;const hp=s.hero.hp;advance(e,.1);assert(s.hero.hp>hp);}
}
for(const gun of [3,4,5]){
 const e=create();e.reset({gun});e.state.spawn=999;
 for(let i=0;i<3;i++){const f=e.spawn(0);Object.assign(f,{x:e.state.hero.x+45+i*(gun===4?10:30),y:e.state.hero.y,hp:gun===5?12:20,speed:0});}
 advance(e,.5);assert.equal(e.state.kills,3,'penetration/AoE/chain '+gun);
}
{
 const e=create();e.reset({zone:1});e.state.spawn=999;Object.assign(e.state.hero,e.state.hazards[0]);advance(e,.1);assert(e.state.hero.hp<e.state.hero.maxHp);
 e.reset();e.state.spawn=999;for(let i=0;i<40;i++)e.spawn();assert.equal(e.state.foes.filter(f=>f.on).length,24);
 for(let i=0;i<50;i++)e.drop(100+i,100,2);assert.equal(e.state.gems.filter(g=>g.on).reduce((n,g)=>n+g.value,0),100);
 e.reset();e.state.spawn=999;e.state.xp=99;advance(e,.1);assert.equal(e.state.mode,'upgrade');assert(e.choose(0));assert.equal(e.state.gunRank,2);
 e.reset();Object.assign(e.state,{gunRank:5,haste:2,magnet:150,orbit:3,rockets:3,xp:99,spawn:999});e.state.pet.rank=3;advance(e,.1);
 assert.equal(new Set(e.state.choices).size,3);assert(e.state.choices.every(key=>!['weapon','haste','magnet','orbit','rockets','pet'].includes(key)),'maxed upgrades cannot waste a choice');
 e.reset();for(let wave=1;wave<=8;wave++){
  e.state.waveTime=0;e.state.spawn=999;
  if(wave%4===0){e.state.bossSpawned=true;e.state.bossDefeated=false;advance(e,.05);assert.equal(e.state.mode,'playing');e.state.bossDefeated=true;}
  advance(e,.05);assert.equal(e.state.mode,wave===8?'won':'upgrade');if(wave!==8){assert(e.choose(1));assert.equal(e.state.wave,wave+1);}
 }
 e.reset();e.hurt(99999);assert.equal(e.state.mode,'dead');
}
console.log('PASS weapons, penetration/AoE/chains, four hero skills, four useful pets, hazards, pools, XP, waves and boss gate');
for(let gun=0;gun<6;gun++){
 const e=create(123+gun);e.reset({hero:0,gun,pet:2,zone:0});let steps=0;
 while(steps++<30000&&!['dead','won'].includes(e.state.mode)){
  const s=e.state,h=s.hero;if(s.mode==='upgrade'){e.choose(0);continue;}
  const t=s.elapsed*.24,tx=640+290*Math.cos(t),ty=480+250*Math.sin(t);let dx=(tx-h.x)/100,dy=(ty-h.y)/100;
  for(const f of s.foes)if(f.on){const x=h.x-f.x,y=h.y-f.y,d=Math.hypot(x,y)||1;if(d<80){dx+=x/d*(80-d)/20;dy+=y/d*(80-d)/20;}}
  e.move(dx,dy);if(s.foes.filter(f=>f.on).length>3)e.skill();e.advance(1/60);e.drainEvents();
 }
 assert.equal(e.state.mode,'won','ordinary movement/skills/upgrades complete all waves with weapon '+gun);
}
console.log('PASS all six weapons complete eight waves using normal controls, without invincibility or enemy/score changes');
const manifest=JSON.parse(source('manifest.json')),page=JSON.parse(source('page.json')),nodes=new Map();
function walk(n){assert(!nodes.has(n.id));nodes.set(n.id,n);for(const c of n.children||[])walk(c);}walk(page.root);assert(nodes.size<=128);assert(!nodes.has('menuHint')&&!nodes.has('menuVersion'));
const images=new Map(manifest.assets.filter(a=>a.type==='image').map(a=>{const b=fs.readFileSync(path.join(folder,a.path));return[a.id,[b.readUInt16LE(4),b.readUInt16LE(6)]];}));
let now=0,seq=0,active=true,homeCalls=0,gameMode=false;
const timers=new Map(),handlers=new Map(),texts=new Map(),frames=new Map(),layouts=new Map(),hidden=new Set(),storage=new Map([['best',17],['settings',{hand:'right',effects:true}],['loadout-v2',{hero:2,gun:3,pet:2,zone:1}]]);let audio=null;
const Ouse={home(){homeCalls++;return true;},game:{setEnabled(value){gameMode=value;}},assets:{readText(id){return source(manifest.assets.find(a=>a.id===id).path);}},display:{get:()=>({orientation:'landscape',contentWidthDp:480,contentHeightDp:320}),setOrientation(){},setFullscreen(){},onChange(){}},timers:{requestFrame(fn){timers.set(++seq,fn);return seq;},clear(id){timers.delete(id);}},storage:{get:key=>Promise.resolve(storage.get(key)),set(key,value){storage.set(key,JSON.parse(JSON.stringify(value)));return Promise.resolve();}},media:{get:()=>audio,play(){audio={active:true,paused:false};return 1;},pause(){if(audio)audio.paused=true;},resume(){if(audio)audio.paused=false;},stop(){audio=null;},setInfo(){},effect(){return true;}},
canvas:{submit(id,commands){assert(active);assert(commands.length<=256);for(const c of commands){for(const [key,value]of Object.entries(c))if(typeof value==='number')assert(Number.isFinite(value)&&Number.isInteger(value),key);if(c.op==='drawImage'){const [w,h]=images.get(c.src);assert(c.sourceX+c.sourceWidth<=w&&c.sourceY+c.sourceHeight<=h);}}frames.set(id,commands);}},
ui:{on(id,event,fn){assert(nodes.has(id),id);handlers.set(id+':'+event,fn);},layout(items){assert(active);assert(items.length<=64);for(const item of items){assert(nodes.has(item.id));for(const n of ['x','y','width','height'])assert(Number.isInteger(item[n]),item.id+' '+n);layouts.set(item.id,item);}},setText(id,value){assert(active);assert(nodes.has(id));texts.set(id,value);},getText:id=>texts.get(id),setStyle(){assert(active);},show(id){assert(active);hidden.delete(id);},hide(id){assert(active);hidden.add(id);},setEnabled(){}}};
const ctx=vm.createContext({Ouse,console,Date:{now:()=>now}}),run=code=>vm.runInContext(code,ctx),click=id=>handlers.get(id+':click')();
function tick(n=1,ms=40){for(let i=0;i<n;i++){now+=ms;for(const [id,fn]of [...timers]){timers.delete(id);fn();}}}
(async()=>{
 run(source('app.js'));await new Promise(setImmediate);tick();assert.equal(run('phase'),'home');assert(!gameMode);assert.equal(run('best'),17);
 click('menuCamp');for(let tab=0;tab<4;tab++){click('tab'+tab);click('equip'+(tab===1?5:tab===3?2:3));tick();}
 assert.deepEqual(JSON.parse(run('JSON.stringify(loadout)')),{hero:3,gun:5,pet:3,zone:2});assert.deepEqual(storage.get('loadout-v2'),{hero:3,gun:5,pet:3,zone:2});
 click('campStart');assert(gameMode);handlers.get('joy:pointerdown')({x:80,y:48});const x=run('game.state.hero.x');tick(10);assert(run('game.state.hero.x')>x);handlers.get('joy:pointercancel')();const stopped=run('game.state.hero.x');tick(3);assert.equal(run('game.state.hero.x'),stopped);
 click('skill');assert(run('game.state.skillCooldown')>0);tick(2);
 run('for(let i=0;i<24;i++){const f=game.spawn(0);if(f)Object.assign(f,{x:game.state.hero.x+(i%6)*20-60,y:game.state.hero.y+Math.floor(i/6)*20-40,hp:5,speed:0});}render()');assert(frames.get('scene').length<=256);
 run('for(const f of game.state.foes)f.on=false');
 click('pause');assert(audio.paused);click('choice0');assert.equal(run('phase'),'game');ctx.app.onPause();active=false;assert.equal(timers.size,0);now+=90000;active=true;ctx.app.onResume();assert.equal(run('phase'),'modal');assert.equal(timers.size,1);click('choice0');tick();
 click('pause');click('choice2');assert.equal(run('phase'),'home');click('settings');click('choice0');click('choice1');assert.equal(storage.get('settings').effects,false);click('choice2');click('menuExit');assert.equal(homeCalls,1);
 ctx.app.onClose();active=false;assert.equal(timers.size,0);assert.equal(audio,null);
 console.log('PASS shared scheduler, camp/loadout persistence, bounded Canvas, joystick/skill, pause/resume and close');
})().catch(e=>{console.error(e);process.exitCode=1;});
