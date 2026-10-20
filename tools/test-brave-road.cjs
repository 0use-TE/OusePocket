const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const folder=path.resolve(__dirname,'../apps/brave-road');
const manifest=JSON.parse(fs.readFileSync(path.join(folder,'manifest.json')));
const page=JSON.parse(fs.readFileSync(path.join(folder,'page.json')));let canvases=0;function inspect(n){if(n.type==='canvas')canvases++;for(const c of n.children||[])inspect(c);}inspect(page.root);assert(canvases<=2);
const hq=process.env.BRAVE_NO_HQ!=='1';
let displayHeight=256;const layoutFrames=new Map();
let now=0,seq=0,submits={world:0,hud:0},lastFrame=[],saved=[],plays=[],effects=[];
let media={active:false,state:'idle'},intervals=new Map(),handlers=new Map();
const ctx=vm.createContext({console,Date:{now:()=>now},Ouse:{
 assets:{loadImage(id,p){assert(p.startsWith("Pictures/"));return hq;},readText(id){return fs.readFileSync(path.join(folder,manifest.assets.find(a=>a.id===id).path),'utf8');}},
 display:{get:()=>({orientation:'landscape',contentWidthDp:480,contentHeightDp:displayHeight}),onChange(){}},
 canvas:{submit(id,commands){assert(id==='world'||id==='hud'||id==='controls');assert(commands.length<=256);for(const c of commands)for(const k of ['x','y','width','height','radius','sourceX','sourceY'])if(c[k]!==undefined)assert(Number.isInteger(c[k]),k);submits[id]=(submits[id]||0)+1;if(id==='world')lastFrame=commands;}},
 ui:{on(id,event,fn){handlers.set(id+':'+event,fn);},layout(frames){for(const f of frames)layoutFrames.set(f.id,f);},setText(){},setEnabled(){},show(){},hide(){}},
 timers:{setInterval(fn,ms){assert(ms>=33);intervals.set(++seq,fn);return seq;},clear(id){intervals.delete(id);}},
 storage:{get:()=>Promise.resolve(null),set(key,value){saved.push([key,JSON.parse(JSON.stringify(value))]);return Promise.resolve();}},
 media:{get:()=>({...media}),play(options){assert(options.path.startsWith('Music/brave-road-'));plays.push(options);media={active:true,state:'playing',paused:false};return plays.length;},pause(){media.paused=true;},resume(){media.paused=false;},stop(){media={active:false,state:'stopped'};},setInfo(){},effect(clip){assert(clip.asset==='sounds'&&clip.offset%2===0&&clip.length%2===0&&clip.length<=64000);effects.push(clip);return true;}}
}});
function run(s){return vm.runInContext(s,ctx);}
function tick(n=1){for(let i=0;i<n;i++){now+=50;for(const fn of [...intervals.values()])fn();}}
async function main(){
 run(fs.readFileSync(path.join(folder,'app.js'),'utf8'));for(let i=0;i<8;i++)await Promise.resolve();
 assert.equal(intervals.size,1);tick();assert.equal(run('phase'),'menu');assert(lastFrame.length);
 for(const h of [240,256]){displayHeight=h;run('layout()');for(const id of ['stick','attack','jump','skill0','skill1','skill2','skill3','hud','pause','modal']){const f=layoutFrames.get(id);assert(f.x>=0&&f.y>=0&&f.x+f.width<=480&&f.y+f.height<=h,id);}assert(layoutFrames.get('skill3').y>layoutFrames.get('skill0').y);assert(layoutFrames.get('skill3').x>400);assert(layoutFrames.get('stick').x<120);assert(layoutFrames.get('attack').x>350);}displayHeight=256;run('layout()');
 assert.equal(run('FPS'),24);assert.equal(run('rules.stageEnemies'),28);
 assert.equal(run('current.enemyArr.map(g=>g[3].length).join()'),'3,3,6,3,4,9');
 assert.equal(run("['l1','l2','l3'].map(k=>[rules.monsters[k].thisAllHp,rules.monsters[k].thisAtt,rules.monsters[k].thisExp].join('/')).join()"),'100/20/5,200/30/10,500/50/20');
 assert.equal(run('[maxHp(),maxMp(),heroAttack(),heroDefense()].join()'),'300,60,25,20');
 assert.equal(run('damage(20,20,1,.99)'),1);assert.equal(run('damage(30,20,1,.99)'),10);assert.equal(run('damage(50,20,1,.99)'),32);
 run('drawHud()');let h=submits.hud;run('drawHud();drawHud()');assert.equal(submits.hud,h,'unchanged HUD must not resubmit');
 handlers.get('play:click')();tick(2);assert.equal(run('phase'),'playing');assert.equal(plays[0].loop,true);
 run('hero.x=100;hero.y=320;hero.onGround=true;hero.vy=0;hero.protect=0;jumpPending=true');run('step()');assert(run('hero.y')<320);
 run('for(let i=0;i<30;i++)step()');assert.equal(run('hero.y'),320);
 run('resetStage();hero.x=215;hero.y=320;hero.onGround=true;frame=9;step()');assert.equal(run('wave'),1);assert.equal(run('queue.length'),2);assert.equal(run('enemies[0].hp'),100);assert.equal(run('lockArea.left'),60);
 run('queue=[];enemies=[];spawn()');assert.equal(run('lockArea'),null);
 run("resetStage();hero.x=100;hero.y=320;hero.onGround=true;hero.protect=0;hero.actionLength=100;hero.action='hurt';spawnEnemy('l2',110,320);enemies[0].target=true;enemies[0].onGround=true;enemies[0].protect=0;for(let i=0;i<5;i++)step()");assert.equal(run('hero.hp'),300,'attack windup cannot deal contact damage');
 run('for(let i=0;i<15;i++)step()');assert(run('hero.hp')<300);
 run("resetStage();spawnEnemy('l1',100,320);enemies[0].hp=1;enemies[0].protect=0;hitEnemy(enemies[0],1,0,0)");assert.equal(run('kills'),1);assert.equal(run('xp'),5);assert(effects.length);
 run('hero.hp=1;hero.mp=0;award(45)');assert.equal(run('[level,xp,hero.hp,hero.mp].join()'),'2,0,330,70');
 run('hero.onGround=true;hero.actionLength=0;doSkill(0)');assert.equal(run('hero.mp'),70);run('level=5;doSkill(0)');assert.equal(run('hero.mp'),60);run('doSkill(0)');assert.equal(run('hero.mp'),60,'action lock prevents duplicate charge');
 run("level=1;hero.hp=100;collect({name:'hps'});collect({name:'s2'})");assert.equal(run('hero.hp'),150);assert.equal(run('heroDefense()'),30);
 for(let i=0;i<32;i++){run(`cameraX=${i}*(worldW-screenW)/31;cameraY=100;render()`);assert(lastFrame.some(c=>c.src===(hq?'village-hq':'village')));}
 for(let w=0;w<6;w++){
  run(`resetStage();wave=${w};cameraX=current.enemyArr[wave][0]*TILE+5;frame=1;spawn()`);
  const expected=run('JSON.stringify(current.enemyArr[wave-1][3])');
  run('while(queue.length){frame+=10;frame-=frame%10;spawn()}');
  assert.equal(run('JSON.stringify(enemies.map(e=>e.name))'),expected);assert(run('enemies.length')<=12);
 }
 run("wave=6;kills=28;queue=[];enemies=[];lockArea=null;spawn()");assert.equal(run('phase'),'clear');tick(6);assert.equal(run('audioStarted'),'win');assert.equal(plays.at(-1).loop,false);
 media={active:false,state:'ended'};const playCount=plays.length;tick(30);assert.equal(plays.length,playCount,'ending must play once');
 handlers.get('restart:click')();handlers.get('stick:pointerdown')({x:75,y:46});assert.equal(run('move'),1);handlers.get('stick:pointermove')({x:46,y:14});assert.equal(run('climb'),-1);handlers.get('stick:pointercancel')();assert.equal(run('move'),0);assert.equal(run('climb'),0);
 run('hero.x=100;hero.y=320;hero.onGround=true;hero.actionLength=0;');handlers.get('attack:pointerdown')();assert(run('attackHeld'));assert(run('hero.actionLength')>0);handlers.get('attack:pointercancel')();assert(!run('attackHeld'));handlers.get('jump:pointerdown')();assert(run('jumpPending'));
 run('level=7;resetStage();setPhase("playing");hero.x=100;hero.y=320;hero.onGround=true;hero.mp=100;');assert(run('doSkill(1)'));assert.equal(run('hero.action'),'roll');assert.equal(run('hero.mp'),80);assert(run('hero.protect')>=28);
 run('hero.actionLength=0;level=14;');assert.equal(run('doSkill(2)'),false);run('level=15;');assert(run('doSkill(2)'));assert.equal(run('hero.action'),'charge');assert.equal(run('hero.mp'),60);run('step()');assert(run('hero.x')>100);
 run('hero.actionLength=0;level=19;');assert.equal(run('doSkill(3)'),false);run('level=20;hero.mp=100;');assert(run('doSkill(3)'));assert.equal(run('hero.mp'),40);assert.equal(run('hero.action'),'slamRise');run('for(let i=0;i<80;i++)step()');assert.notEqual(run('hero.action'),'slamFall');assert(run('hero.onGround'));
 run('hero.actionLength=0;hero.mp=0;');assert.equal(run('doSkill(0)'),false);
 run('equipment.dress=1;hero.hp=100;const previousHp=maxHp();collect({name:"d2"});');assert.equal(run('hero.hp'),220);
 run('resetStage();setPhase("playing");hero.onGround=true;hero.actionLength=0;hero.x=100;hero.y=320;hero.face=1;lockArea={left:60,right:115};level=15;doSkill(2);step()');assert(run('hero.x')<=105);
 run('resetStage();setPhase("playing");hero.x=100;hero.y=320;hero.onGround=true;spawnEnemy("l1",115,320);enemies[0].protect=0;step()');assert.equal(run('hero.actionLength'),0,'manual attack only; no automatic melee');
 ctx.app.onPause();assert.equal(intervals.size,0);assert.equal(run('move'),0);assert(!run('attackHeld'));assert.equal(run('phase'),'paused');ctx.app.onResume();assert.equal(intervals.size,1);assert.equal(run('phase'),'menu');ctx.app.onClose();assert.equal(intervals.size,0);assert.equal(media.active,false);
 assert(saved.every(([key])=>key==='first-stage-v2'));
 console.log('PASS first-stage 28 enemies / 6 waves / 3 distinct monster rules, damage and growth, windup, ground/jump, original skill level gates/costs/action phases, manual input/landscape controls, pickups, cached HUD, rendering bounds, audio lifecycle, save and cancellation');
}
main().catch(e=>{console.error(e);process.exitCode=1;});



