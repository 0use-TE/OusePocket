'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const page=JSON.parse(fs.readFileSync('apps/survivor/page.json','utf8')),nodes=new Map(),parents=new Map();
function walk(n,parent){assert(!nodes.has(n.id));nodes.set(n.id,n);parents.set(n.id,parent);for(const c of n.children||[])walk(c,n.id);}walk(page.root,null);assert(nodes.size<=128);
for(const [w,h]of [[320,416],[480,256],[480,240]]){function valid(n,pw,ph){const nw=n.width==='fill'?pw:n.width,nh=n.height==='fill'?ph:n.height;assert((n.x||0)+nw<=pw,n.id+' width');assert((n.y||0)+nh<=ph,n.id+' height');for(const c of n.children||[])valid(c,nw,nh);}valid(page.root,w,h);}
let active=true,homeCalls=0;
let now=1000,next=1,onChange;const intervals=new Map(),handlers=new Map(),frames=new Map(),actors=new Map(),texts=new Map(),visible=new Map(),store=new Map();
const display={orientation:'portrait',contentWidthDp:320,contentHeightDp:416};
const Ouse={home:()=>{homeCalls++;return true;},display:{get:()=>display,onChange:f=>onChange=f,setOrientation(o){display.orientation=o;display.contentWidthDp=o==='landscape'?480:320;display.contentHeightDp=o==='landscape'?256:416;onChange();return true;}},ui:{layout(p){assert(active,"background UI mutation");assert(p.length<=64);for(const f of p){assert(nodes.has(f.id));for(const k of ['x','y','width','height'])assert(Number.isInteger(f[k]),f.id+k);assert(f.width>0&&f.height>0);frames.set(f.id,f);}},on:(id,e,f)=>handlers.set(id+':'+e,f),setEnabled(id,value){assert(nodes.has(id));},setText:(id,t)=>texts.set(id,t),show:id=>visible.set(id,true),hide:id=>visible.set(id,false),setStyle(id,style){assert(active);assert(nodes.has(id));}},sprites:{update(p){assert(p.length<=32);for(const v of p){assert(nodes.has(v.id));assert(v.frame===undefined||(v.frame>=0&&v.frame<52));actors.set(v.id,v);}}},storage:{get:k=>Promise.resolve(store.get(k)),set(k,v){store.set(k,v);return Promise.resolve(true);}},timers:{setInterval(f,ms){assert.equal(ms,50);let id=next++;intervals.set(id,f);return id;},clear:id=>intervals.delete(id)}};
class Clock extends Date{static now(){return now;}}
const c=vm.createContext({Ouse,Date:Clock,console});vm.runInContext(fs.readFileSync('apps/survivor/app.js','utf8'),c);
const run=code=>vm.runInContext(code,c),click=id=>handlers.get(id+':click')(),pointer=(type,x,y)=>handlers.get('joy:'+type)({x,y});
function ticks(n){for(let i=0;i<n;i++){now+=50;for(const f of [...intervals.values()])f();}}
(async()=>{
 await new Promise(setImmediate);assert.equal(display.orientation,'landscape');assert.equal(intervals.size,1);assert.equal(run('phase'),'intro');ticks(36);assert.equal(run('phase'),'menu');assert.equal(intervals.size,0);assert.equal(texts.get('menuVersion').slice(0,6),'V1.0.0');
 click('menuSettings');assert.equal(run('phase'),'settings');click('choice0');assert.equal(run('preferences.hand'),'right');click('choice1');assert.equal(run('preferences.effects'),false);assert.equal(store.get('settings').hand,'right');click('choice2');assert.equal(run('phase'),'menu');click('menuSettings');click('choice0');click('choice1');click('choice2');
 click('menuStart');assert.equal(run('phase'),'running');let x=run('px');pointer('pointerdown',88,54);ticks(10);assert(run('px')>x);pointer('pointerup',0,0);x=run('px');ticks(10);assert.equal(run('px'),x);assert.equal(run('mx'),0);
 pointer('pointerdown',54,20);pointer('pointercancel',0,0);assert.equal(run('my'),0);
 run('for(const p of foes)p.on=false;spawnClock=100;foes[0].on=true;foes[0].x=px+45;foes[0].y=py;foes[0].hp=1;foes[0].speed=0;foes[0].type=0;fireClock=0;');ticks(15);assert(run('kills')>=1);assert(run('gems.some(g=>g.on)')||run('xp')>0);
 click('pause');const elapsed=run('elapsed');ticks(30);assert.equal(run('elapsed'),elapsed);assert.equal(run('phase'),'pause');click('choice0');assert.equal(run('phase'),'running');
 active=false;c.app.onPause();assert.equal(intervals.size,0);assert.equal(run('phase'),'pause');active=true;c.app.onResume();assert.equal(intervals.size,0);assert.equal(run('phase'),'pause');click('choice0');
 // Rotation request returns to landscape, without resetting the run.
 const kills=run('kills');Ouse.display.setOrientation('portrait');assert.equal(display.orientation,'landscape');assert.equal(run('kills'),kills);assert.equal(intervals.size,1);
 // All 8 wave transitions, upgrades and victory; no survivor timer leak.
 for(let i=1;i<=8;i++){run('hp=maxHp=100000;seconds=.01;');ticks(1);assert.equal(run('phase'),i===8?'won':'upgrade');if(i<8)click('choice0');}
 assert.equal(run('wave'),8);assert(visible.get('modal'));assert(store.get('best')>=1);
 click('choice1');assert.equal(run('phase'),'menu');click('menuStart');run('hp=maxHp=100000;damage=1;rocketRank=orbitRank=0;');ticks(300);assert.equal(run('foes.length'),12);assert.equal(run('shots.length'),6);assert.equal(run('gems.length'),8);assert(run('foes.filter(p=>p.on).length')>=8);
 run('seconds=.01');ticks(1);assert.equal(run('phase'),'upgrade');click('choice0');assert.equal(run('wave'),2);
 run('start(false);spawnClock=100;');
 for(const [dx,dy,frame] of [[60,0,16],[0,60,24],[-60,0,32],[0,-60,40],[50,50,20]]){
  run(`for(const p of foes)p.on=false;for(const b of shots)b.on=false;foes[0].on=true;foes[0].x=px+${dx};foes[0].y=py+${dy};foes[0].hp=100;foes[0].speed=0;fireClock=0;update(0);render();`);
  assert.equal(actors.get('gun').frame,frame);
  const aim=run('[aimDX,aimDY]'),bullet=run('[shots[0].dx,shots[0].dy]');assert(Math.abs(aim[0]-bullet[0]/245)<1e-8);assert(Math.abs(aim[1]-bullet[1]/245)<1e-8);
 }
 assert.equal(run('gunFrame(1,0)'),16);assert.equal(run('gunFrame(0,1)'),24);assert.equal(run('gunFrame(-1,0)'),32);assert.equal(run('gunFrame(0,-1)'),40);assert.equal(run('gunFrame(1,1)'),20);
 // Pooled overflow merges gem value, and swept bullet collision prevents tunneling.
 run('for(const g of gems){g.on=true;g.x=100;g.y=100;g.value=1;}drop(100,100,3)');assert.equal(run('gems.reduce((s,g)=>s+g.value,0)'),11);assert.equal(run('segmentDistance(50,0,0,0,100,0)'),0);
 run('hp=1;invincible=0;foes[0].on=true;foes[0].x=px;foes[0].y=py;foes[0].speed=0;');ticks(1);assert.equal(run('phase'),'dead');
 for(const [id,f]of frames){const p=parents.get(id),parent=p?frames.get(p)||nodes.get(p):{width:480,height:256};if(id.startsWith('blast'))continue;assert(f.x>=0&&f.y>=0,id);if(parent.width!=='fill')assert(f.x+f.width<=parent.width,id+' right');if(parent.height!=='fill')assert(f.y+f.height<=parent.height,id+' bottom');}

 // Real area damage hits multiple enemies once and range scales the radius.
 run('start(false);spawnClock=100;px=300;py=100;rocketRank=1;for(let i=0;i<3;i++){Object.assign(foes[i],{on:true,x:100+i*25,y:100,hp:20,speed:0,type:0,slow:0});}explode(100,100);render();');
 assert.equal(run('kills'),2);assert(run('foes[2].on'));const radius=run('blasts[0].radius');run('rangeScale=1.5;explode(50,50);render()');assert(run('blasts[0].radius')>radius);
 run('start(false);spawnClock=100;rocketRank=1;fireClock=rocketClock=100;Object.assign(foes[0],{on:true,x:px+35,y:py,hp:20,speed:0,type:0});Object.assign(rockets[0],{on:true,x:px+35,y:py,dx:1,dy:0,life:1,target:foes[0]});update(0);');assert.equal(run('foes[0].on'),false);assert(run('blasts.some(b=>b.on)'));
 run('start(false);spawnClock=100;orbitRank=1;fireClock=100;Object.assign(foes[0],{on:true,x:px+30,y:py,hp:10,speed:0,type:0});update(0);');assert.equal(run('foes[0].on'),false);assert.equal(run('swords.filter(s=>s.on).length'),1);
 run('start(false);armor=4;hurt(9)');assert.equal(run('hp'),75);run('invincible=0;hp=50;regen=2;spawnClock=100;update(1)');assert.equal(run('hp'),52);
 run('start();wave=4;for(let i=0;i<12;i++)spawn();');assert(run('foes.filter(p=>p.on).length')<=12);
 run('hp=maxHp=100000;');ticks(1200);assert(run('pools.every(pool=>pool.length<=12)'));
 run('menu()');click('menuExit');assert.equal(homeCalls,1);assert.equal(run('phase'),'exit');assert.equal(intervals.size,0);active=false;c.app.onPause();active=true;c.app.onResume();assert.equal(run('phase'),'intro');ticks(36);assert.equal(run('phase'),'menu');
 active=false;c.app.onClose();assert.equal(intervals.size,0);console.log('PASS survivor: cold schema, loading/menu, persisted settings/exit, landscape, joystick release/cancel, aiming/hits, pause/resume, 8 waves/upgrades/victory, defeat, bounded stress pools, gem overflow and teardown; AoE/range, rockets, orbit swords, armor/regen, three enemy types, reduced pools; '+nodes.size+' nodes');
})().catch(e=>{console.error(e);process.exitCode=1;});
