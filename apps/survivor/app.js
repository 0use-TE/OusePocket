// Shared foreground game scheduler: one UI-paced frame, no frame backlog.
function createGameRuntime(onFrame,maxFps=25){
 let id=0,running=false,next=0,since=0,frames=0,fps=0;const period=maxFps>0?1000/maxFps:0;
 function frame(){id=0;if(!running)return;const now=Date.now();if(now>=next){const target=next+period;next=target>now?target:now+period;frames++;if(now-since>=1000){fps=Math.round(frames*1000/(now-since));frames=0;since=now;}onFrame();}if(running&&!id)id=Ouse.timers.requestFrame(frame);}
 return {start(){if(running)return;running=true;next=since=Date.now();frames=fps=0;id=Ouse.timers.requestFrame(frame);},stop(){running=false;if(id)Ouse.timers.clear(id);id=0;fps=0;},get running(){return running;},get fps(){return fps;}};
}

// Deterministic, fixed-step combat simulation. UI and audio stay outside this file.
const SurvivorContent={
 heroes:[
  {name:'游侠',hp:110,speed:104,armor:0,damage:1.1,skill:'翻滚',cooldown:5,description:'均衡火力\n翻滚躲避攻击'},
  {name:'重装',hp:170,speed:82,armor:3,damage:1,skill:'铁壁',cooldown:12,description:'高生命与护甲\n铁壁抵挡伤害'},
  {name:'工程师',hp:100,speed:96,armor:1,damage:1,skill:'哨戒炮',cooldown:14,description:'伙伴伤害提升\n部署自动炮台'},
  {name:'疾风',hp:90,speed:126,armor:0,damage:.95,skill:'闪身',cooldown:4,description:'高速移动\n闪身突破包围'}],
 guns:[
  {name:'突击步枪',damage:17,rate:.34,speed:350,pellets:1,pierce:1,description:'均衡射速\n稳定单体输出'},
  {name:'散弹枪',damage:12,rate:.85,speed:300,pellets:5,pierce:1,description:'五发散射\n近距离清群'},
  {name:'冲锋枪',damage:8,rate:.115,speed:380,pellets:1,pierce:1,description:'高速连射\n适合持续压制'},
  {name:'狙击枪',damage:62,rate:1.05,speed:600,pellets:1,pierce:3,description:'高伤穿透\n贯穿三名敌人'},
  {name:'榴弹炮',damage:34,rate:1.05,speed:220,pellets:1,pierce:1,description:'爆炸范围伤害\n适合密集敌群'},
  {name:'电弧枪',damage:20,rate:.65,speed:0,pellets:1,pierce:1,description:'连锁电击\n弹射附近敌人'}],
 pets:[
  {name:'护卫犬',description:'冲向附近敌人\n持续近战支援'},
  {name:'磁力猫',description:'吸引远处经验\n加速角色成长'},
  {name:'医疗蜂',description:'定时恢复生命\n提升生存能力'},
  {name:'火尾狐',description:'周期释放火环\n灼烧周围敌群'}],
 zones:[{name:'荒野草原',description:'开阔草地\n适合熟悉武器'}, {name:'锈蚀工厂',description:'毒池侵蚀\n留意脚下区域'}, {name:'霜夜营地',description:'冰面减速\n快速怪物更多'}]
};
function createSurvivorEngine(seed=1){
 const dt=1/60,W=1280,H=960,MAX_FOES=24,MAX_SHOTS=32;
 const native=typeof Ouse!=='undefined'&&Ouse.game2d&&Ouse.game2d.version===1?Ouse.game2d:null;
 const shotData=native?new Float64Array(MAX_SHOTS*8):null,targetData=native?new Float64Array(MAX_FOES*4):null,hitData=native?new Uint32Array(MAX_SHOTS):null;
 let randomState=seed>>>0,s,acc=0,mx=0,my=0,events=[];
 const pool=n=>Array.from({length:n},(_,id)=>({id,on:false}));
 function random(){randomState=(Math.imul(randomState,1664525)+1013904223)>>>0;return randomState/4294967296;}
 const clamp=(v,a,b)=>Math.max(a,Math.min(b,v)),dist2=(a,b)=>(a.x-b.x)**2+(a.y-b.y)**2;
 const hazards=[{x:330,y:290,r:62},{x:940,y:420,r:72},{x:660,y:760,r:65}];
 function reset(loadout={hero:0,gun:0,pet:0,zone:0}){
  const choice={};for(const [key,items]of Object.entries({hero:SurvivorContent.heroes,gun:SurvivorContent.guns,pet:SurvivorContent.pets,zone:SurvivorContent.zones}))choice[key]=clamp(loadout[key]|0,0,items.length-1);
  const hero=SurvivorContent.heroes[choice.hero];
  s={mode:'playing',loadout:choice,width:W,height:H,hero:{x:W/2,y:H/2,hp:hero.hp,maxHp:hero.hp,speed:hero.speed,armor:hero.armor,invincible:0,dx:1,dy:0},
   pet:{x:W/2-32,y:H/2+24,rank:1,cooldown:0},foes:pool(MAX_FOES),shots:pool(MAX_SHOTS),gems:pool(18),enemyShots:pool(12),effects:pool(8),
   turret:{x:0,y:0,time:0,clock:0},hazards,elapsed:0,wave:1,waveTime:25,level:1,xp:0,nextXp:9,kills:0,gunRank:1,attack:1,haste:1,magnet:52,regen:0,orbit:0,rockets:0,
   fire:0,spawn:1,skillCooldown:0,skillTime:0,petKills:0,choices:[],upgradeReason:'',pendingLevel:0,bossSpawned:false,bossDefeated:false,spawnSerial:0};
  mx=my=acc=0;events=[];return s;
 }
 function move(x,y){const d=Math.hypot(x,y);mx=d>1?x/d:x;my=d>1?y/d:y;}
 function effect(kind,x,y,r=22,x2=x,y2=y){const f=s.effects.find(f=>!f.on);if(f)Object.assign(f,{on:true,kind,x,y,x2,y2,r,life:.25});}
 function drop(x,y,value){let gem=s.gems.find(g=>!g.on);if(!gem){gem=s.gems.reduce((a,b)=>dist2(a,{x,y})<dist2(b,{x,y})?a:b);gem.value+=value;return;}Object.assign(gem,{on:true,x,y,value});}
 function hit(foe,damage,pet=false){
  if(!foe.on)return;foe.hp-=damage;foe.flash=.1;
  if(foe.hp>0)return;
  foe.on=false;s.kills++;if(pet)s.petKills++;drop(foe.x,foe.y,foe.boss?18:foe.type===2?4:2);
  if(foe.boss){s.bossDefeated=true;s.hero.hp=Math.min(s.hero.maxHp,s.hero.hp+35);events.push('boss');}else if(s.kills%12===0){s.hero.hp=Math.min(s.hero.maxHp,s.hero.hp+4);}
 }
 function hurt(amount){
  const h=s.hero;if(h.invincible>0||s.mode!=='playing')return;
  h.hp=Math.max(0,h.hp-Math.max(1,amount-h.armor)*(s.loadout.hero===1&&s.skillTime>0?.15:1));h.invincible=.65;events.push('hurt');
  if(h.hp<=0){s.mode='dead';move(0,0);events.push('dead');}
 }
 function blast(x,y,r,damage,pet=false){effect('blast',x,y,r);for(const f of s.foes)if(f.on&&dist2(f,{x,y})<(r+f.radius)**2)hit(f,damage,pet);}
 function spawn(type=null,boss=false){
  const f=s.foes.find(f=>!f.on);if(!f)return null;
  const a=random()*Math.PI*2,d=270+random()*60;
  type=type===null?(random()<.2?2:random()<.35?1:s.wave>=3&&random()<.3?3:0):type;
  const health=(boss?420+s.wave*60:[22,15,62,34][type])+s.wave*3;
  Object.assign(f,{on:true,x:clamp(s.hero.x+Math.cos(a)*d,30,W-30),y:clamp(s.hero.y+Math.sin(a)*d,30,H-30),type,boss,hp:health,maxHp:health,radius:boss?24:type===2?15:11,
   speed:boss?36:[30,60,22,25][type]+s.wave*1.4+(s.loadout.zone===2?5:0),clock:1+random(),flash:0,charge:0,aimX:0,aimY:0,serial:++s.spawnSerial});
  return f;
 }
 function nearest(from,range=Infinity,excluded=0){let best=null,d=range*range;for(const f of s.foes)if(f.on&&!(excluded&(1<<f.id))){const n=dist2(from,f);if(n<d){best=f;d=n;}}return best;}
 function shoot(from,target,gunIndex,damageScale=1){
  if(!target)return;
  const gun=SurvivorContent.guns[gunIndex],h=s.hero,baseDamage=gun.damage*s.attack*(1+(s.gunRank-1)*.25)*SurvivorContent.heroes[s.loadout.hero].damage*damageScale;
  const angle=Math.atan2(target.y-from.y,target.x-from.x);h.dx=Math.cos(angle);h.dy=Math.sin(angle);
  if(gunIndex===5){
   let previous=from,hitMask=0,current=target;
   for(let i=0;i<3+(s.gunRank>=5?2:0)&&current;i++){
    effect('arc',previous.x,previous.y,0,current.x,current.y);hitMask|=1<<current.id;hit(current,baseDamage*(1-i*.12));previous=current;current=nearest(previous,130,hitMask);
   }
  }else for(let i=0;i<gun.pellets;i++){
   const b=s.shots.find(b=>!b.on);if(!b)break;
   const a=angle+(i-(gun.pellets-1)/2)*.17;
   Object.assign(b,{on:true,x:from.x+Math.cos(a)*16,y:from.y+Math.sin(a)*16,dx:Math.cos(a)*gun.speed,dy:Math.sin(a)*gun.speed,damage:baseDamage,gun:gunIndex,life:gunIndex===1?.48:1.7,pierce:gun.pierce+(s.gunRank>=5&&gunIndex===3?2:0),mask:0});
  }
  events.push('shot');
 }
 function skill(){
  if(s.mode!=='playing'||s.skillCooldown>0)return false;
  const kind=s.loadout.hero;s.skillCooldown=SurvivorContent.heroes[kind].cooldown;s.skillTime=kind===1?4:kind===2?8:.28;
  if(kind===0||kind===3)s.hero.invincible=.6;
  if(kind===2)Object.assign(s.turret,{x:s.hero.x,y:s.hero.y,time:8,clock:0});
  events.push('skill');return true;
 }
 function upgrade(reason){
  if(s.mode!=='playing')return;
  s.mode='upgrade';s.upgradeReason=reason;move(0,0);
  const deck=['attack','haste','health','pet','armor','magnet','orbit','rockets','regen'].filter(key=>key==='pet'?s.pet.rank<3:key==='haste'?s.haste<2:key==='magnet'?s.magnet<150:key==='orbit'?s.orbit<3:key==='rockets'?s.rockets<3:true);
  const picks=[s.gunRank<5?'weapon':'attack',deck[(s.wave+s.level)%deck.length],deck[(s.wave+s.level+4)%deck.length]];
  if(s.pet.rank<3&&s.level%3===0)picks[1]='pet';
  for(let i=1;i<picks.length;i++)if(picks.slice(0,i).includes(picks[i]))picks[i]=deck.find(key=>!picks.slice(0,i).includes(key));
  s.choices=picks;events.push('upgrade');
 }
 function choose(index){
  if(s.mode!=='upgrade'||index<0||index>=s.choices.length)return false;
  const key=s.choices[index],h=s.hero;
  if(key==='weapon')s.gunRank=Math.min(5,s.gunRank+1);
  if(key==='attack')s.attack*=1.2;if(key==='haste')s.haste=Math.min(2,s.haste*1.15);
  if(key==='health'){h.maxHp+=25;h.hp=Math.min(h.maxHp,h.hp+55);}
  if(key==='pet')s.pet.rank=Math.min(3,s.pet.rank+1);
  if(key==='armor')h.armor+=2;if(key==='magnet')s.magnet=Math.min(150,s.magnet+25);
  if(key==='orbit')s.orbit=Math.min(3,s.orbit+1);if(key==='rockets')s.rockets=Math.min(3,s.rockets+1);if(key==='regen')s.regen+=.4;
  if(s.upgradeReason==='wave'){s.wave++;s.waveTime=25;s.bossSpawned=false;s.bossDefeated=false;for(const f of s.foes)f.on=false;for(const f of s.enemyShots)f.on=false;h.hp=Math.min(h.maxHp,h.hp+15);}
  s.mode='playing';s.choices=[];s.fire=0;acc=0;events.push('select');return true;
 }
 function segmentDistance(f,ax,ay,bx,by){const dx=bx-ax,dy=by-ay,len=dx*dx+dy*dy,t=len?clamp(((f.x-ax)*dx+(f.y-ay)*dy)/len,0,1):0;return (f.x-ax-dx*t)**2+(f.y-ay-dy*t)**2;}
 function step(){
  if(s.mode!=='playing')return;
  const h=s.hero;s.elapsed+=dt;s.waveTime=Math.max(0,s.waveTime-dt);h.invincible=Math.max(0,h.invincible-dt);h.hp=Math.min(h.maxHp,h.hp+s.regen*dt);
  s.skillCooldown=Math.max(0,s.skillCooldown-dt);s.skillTime=Math.max(0,s.skillTime-dt);
  let velocity=h.speed;
  if(s.skillTime>0&&(s.loadout.hero===0||s.loadout.hero===3))velocity=s.loadout.hero===3?420:330;
  if(s.loadout.zone)for(const p of hazards)if(dist2(h,p)<p.r*p.r){if(s.loadout.zone===1)hurt(6);else velocity*=.6;}
  if(s.mode!=='playing')return;
  const dx=mx||my?mx:s.skillTime>0&&s.loadout.hero!==1&&s.loadout.hero!==2?h.dx:0,dy=mx||my?my:s.skillTime>0&&s.loadout.hero!==1&&s.loadout.hero!==2?h.dy:0;
  h.x=clamp(h.x+dx*velocity*dt,18,W-18);h.y=clamp(h.y+dy*velocity*dt,18,H-18);
  s.spawn-=dt;if(s.spawn<=0){spawn();s.spawn=Math.max(.36,.95-s.wave*.065);}
  if(s.wave%4===0&&!s.bossSpawned&&s.waveTime<=22){spawn(2,true);s.bossSpawned=true;events.push('bossSpawn');}
  for(const f of s.foes)if(f.on){
   f.flash=Math.max(0,f.flash-dt);f.clock-=dt;const vx=h.x-f.x,vy=h.y-f.y,d=Math.hypot(vx,vy)||1;
   let speed=f.type===3&&d<150?0:f.speed;
   if(f.boss){
    if(f.clock<0){f.clock=4;f.charge=.7;f.aimX=vx/d;f.aimY=vy/d;}
    if(f.charge>0){f.charge-=dt;if(f.charge>.28)speed=0;else {speed=230;f.x+=f.aimX*speed*dt;f.y+=f.aimY*speed*dt;speed=0;}}
   }
   f.x=clamp(f.x+vx/d*speed*dt,10,W-10);f.y=clamp(f.y+vy/d*speed*dt,10,H-10);
   if(f.type===3&&!f.boss&&f.clock<=0){const b=s.enemyShots.find(b=>!b.on);if(b)Object.assign(b,{on:true,x:f.x,y:f.y,dx:vx/d*125,dy:vy/d*125,life:3});f.clock=2.2;}
   if(d<f.radius+10)hurt(f.boss?24:f.type===2?17:10);
   if(s.mode!=='playing')return;
  }
  const target=nearest(h,430);s.fire-=dt;
  if(target&&s.fire<=0){shoot(h,target,s.loadout.gun);s.fire=SurvivorContent.guns[s.loadout.gun].rate/s.haste*(s.gunRank>=5?.85:1);}
  if(native){
   for(let i=0;i<MAX_FOES;i++){const f=s.foes[i],k=i*4;targetData[k]=f.on?f.x:0;targetData[k+1]=f.on?f.y:0;targetData[k+2]=f.on?f.radius:0;targetData[k+3]=f.on?1:0;}
   for(let i=0;i<MAX_SHOTS;i++){const b=s.shots[i],k=i*8;shotData[k]=b.on?b.x:0;shotData[k+1]=b.on?b.y:0;shotData[k+2]=b.on?b.dx:0;shotData[k+3]=b.on?b.dy:0;shotData[k+4]=b.on?b.life:0;shotData[k+5]=3;shotData[k+6]=b.on?1:0;shotData[k+7]=b.on?b.mask>>>0:0;}
   native.stepProjectiles(shotData.buffer,targetData.buffer,hitData.buffer,dt);
  }
  for(const b of s.shots)if(b.on){
   const x=b.x,y=b.y;
   if(native){const k=b.id*8;b.x=shotData[k];b.y=shotData[k+1];b.life=shotData[k+4];}
   else {b.x+=b.dx*dt;b.y+=b.dy*dt;b.life-=dt;}
   for(const f of s.foes)if(f.on&&!(b.mask&(1<<f.id))&&(native?!!(hitData[b.id]&(1<<f.id)):segmentDistance(f,x,y,b.x,b.y)<(f.radius+3)**2)){
    b.mask|=1<<f.id;if(b.gun===4){blast(b.x,b.y,48+s.gunRank*4,b.damage);b.on=false;break;}
    hit(f,b.damage);if(--b.pierce<=0){b.on=false;break;}
   }
   if(b.life<=0||b.x<0||b.x>W||b.y<0||b.y>H){if(b.on&&b.gun===4)blast(b.x,b.y,45,b.damage);b.on=false;}
  }
  for(const b of s.enemyShots)if(b.on){b.x+=b.dx*dt;b.y+=b.dy*dt;b.life-=dt;if(dist2(b,h)<144){hurt(12);b.on=false;}if(b.life<=0)b.on=false;}
  if(s.mode!=='playing')return;
  const p=s.pet;p.cooldown-=dt;const petPower=(s.loadout.hero===2?1.35:1)*(1+(p.rank-1)*.4),petTarget=nearest(p,150);
  const dest=s.loadout.pet===0&&petTarget?petTarget:{x:h.x-28,y:h.y+22};
  p.x+=(dest.x-p.x)*Math.min(1,dt*6);p.y+=(dest.y-p.y)*Math.min(1,dt*6);
  if(p.cooldown<=0){
   if(s.loadout.pet===0&&petTarget&&dist2(p,petTarget)<1600){hit(petTarget,19*petPower,true);effect('bite',petTarget.x,petTarget.y,15);p.cooldown=.75;}
   if(s.loadout.pet===2){h.hp=Math.min(h.maxHp,h.hp+9*petPower);effect('heal',h.x,h.y,24);p.cooldown=5;}
   if(s.loadout.pet===3&&petTarget){blast(p.x,p.y,78,20*petPower,true);p.cooldown=2.6;}
  }
  if(s.turret.time>0){s.turret.time-=dt;s.turret.clock-=dt;const t=nearest(s.turret,300);if(t&&s.turret.clock<=0){shoot(s.turret,t,0,.65);s.turret.clock=.4;}}
  if(s.orbit)for(let i=0;i<Math.min(2,s.orbit);i++){const a=s.elapsed*3+i*Math.PI,x=h.x+Math.cos(a)*45,y=h.y+Math.sin(a)*45;for(const f of s.foes)if(f.on&&(f.orbitAt||0)<s.elapsed&&dist2(f,{x,y})<324){hit(f,12+s.orbit*5);f.orbitAt=s.elapsed+.45;}}
  if(s.rockets&&target&&Math.floor(s.elapsed*60)%180===0)blast(target.x,target.y,52,24+s.rockets*15);
  for(const g of s.gems)if(g.on){const d=Math.sqrt(dist2(g,h));if(d<18){s.xp+=g.value;g.on=false;events.push('fruit');}else if(d<s.magnet+(s.loadout.pet===1?95+p.rank*20:0)){g.x+=(h.x-g.x)/d*190*dt;g.y+=(h.y-g.y)/d*190*dt;}}
  for(const f of s.effects)if(f.on){f.life-=dt;if(f.life<=0)f.on=false;}
  if(s.mode!=='playing')return;
  if(s.xp>=s.nextXp){s.xp-=s.nextXp;s.level++;s.nextXp=Math.round(s.nextXp*1.25+2);upgrade('level');}
  else if(s.waveTime<=0&&(s.wave%4!==0||s.bossDefeated)){
   if(s.wave===8){s.mode='won';move(0,0);events.push('won');}else upgrade('wave');
  }
 }
 function advance(seconds){if(!Number.isFinite(seconds)||seconds<=0)return;acc+=Math.min(.125,seconds);let steps=0;while(acc>=dt&&steps++<8){step();acc-=dt;}}
 function pause(){if(s.mode==='playing'){s.mode='paused';move(0,0);acc=0;}}
 function resume(){if(s.mode==='paused'){s.mode='playing';acc=0;}}
 reset();return {backend:native?"native":"js",reset,move,advance,skill,choose,pause,resume,spawn,hit,hurt,blast,drop,drainEvents(){const result=events;events=[];return result;},get state(){return s;},content:SurvivorContent};
}

// Native playback/mixing owns PCM work. Only control messages run in the game loop.
function createSurvivorAudio(host){
 const clips=JSON.parse(host.assets.readText('audio'));
 let wanted=false,closed=false,nextCheck=0,retryAt=0,nextReport=1000,effects=0;
 function update(now){
  if(closed||!wanted||now<nextCheck)return;nextCheck=now+250;
  const state=host.media.get();
  if(state&&state.active){
   if(state.paused)host.media.resume();
   if(now>=nextReport){nextReport=now+10000;console.log('SURVIVOR_AUDIO '+JSON.stringify({positionMs:state.positionMs||0,effects,error:state.errorCode||0}));}
   return;
  }
  if(now<retryAt)return;retryAt=now+2000;
  if(host.media.play({asset:'music',loop:true}))host.media.setInfo({title:'荒野生存',artist:'荒野行军曲'});
 }
 function sync(enabled){
  if(closed)return;
  if(wanted===enabled)return;wanted=enabled;nextCheck=0;
  if(!wanted)host.media.pause();else update(Date.now());
 }
 function effect(name){
  if(closed||!wanted)return false;const clip=clips[name];if(!clip)return false;
  const accepted=host.media.effect({asset:'sounds',offset:clip.offset,length:clip.length,gain:85});
  if(accepted)effects++;return accepted;
 }
 return {sync,update,effect,close(){if(closed)return;closed=true;wanted=false;host.media.stop();},get effects(){return effects;}};
}

const game=createSurvivorEngine(),content=game.content,sound=createSurvivorAudio(Ouse),loop=createGameRuntime(tick,0);
console.log('SURVIVOR_ENGINE '+game.backend);
let width=480,height=320,phase='home',active=true,closed=false,last=0,clock=0,camX=0,camY=0,held=false,joyX=0,joyY=0,tab=0,ready=false,best=0,hudKey='',shotAt=0,savedResult=false,reportAt=0;
let loadout={hero:0,gun:0,pet:0,zone:0},preferences={hand:'left',effects:true};
const tabKeys=['hero','gun','pet','zone'],lists=[content.heroes,content.guns,content.pets,content.zones];
const upgradeLabels={weapon:'枪械强化',attack:'火力提升 20%',haste:'射速提升 15%',health:'生命提升与恢复',pet:'伙伴进阶',armor:'护甲提升',magnet:'扩大拾取范围',orbit:'环绕飞剑',rockets:'爆破支援',regen:'持续恢复生命'};
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
function stopStick(){held=false;joyX=joyY=0;game.move(0,0);hudKey='';}
function persist(){Ouse.storage.set('loadout-v2',loadout).catch(()=>{});Ouse.storage.set('settings',preferences).catch(()=>{});}
function sync(){
 if(!active||closed)return;
 const playing=phase==='game';Ouse.game.setEnabled(playing||phase==='modal');sound.sync(phase!=='modal'||game.state.mode!=='paused');
 for(const [id,on]of [['home',phase==='home'],['camp',phase==='camp'],['modal',phase==='modal'],['joy',playing],['skill',playing],['pause',playing]])on?Ouse.ui.show(id):Ouse.ui.hide(id);
 Ouse.ui.setEnabled('menuStart',ready);Ouse.ui.setEnabled('campStart',ready);
 if(phase==='camp'){
  const items=lists[tab];for(let i=0;i<6;i++){if(i<items.length){Ouse.ui.show('equip'+i);Ouse.ui.setText('equipName'+i,items[i].name);Ouse.ui.setStyle('equipName'+i,{textColor:loadout[tabKeys[tab]]===i?'#294638':'#efebcd'});}else Ouse.ui.hide('equip'+i);}
  for(let i=0;i<4;i++)Ouse.ui.setStyle('tab'+i,{backgroundColor:i===tab?'#d6cf9b':'#35594b',textColor:i===tab?'#294638':'#efebcd'});
  const item=items[loadout[tabKeys[tab]]];Ouse.ui.setText('previewName',item.name);Ouse.ui.setText('previewInfo',item.description);
 }
 hudKey='';
}
function camera(snap=false,dt=.05){const h=game.state.hero,x=clamp(h.x-width/2,0,game.state.width-width),y=clamp(h.y-height/2,0,game.state.height-height),a=snap?1:1-Math.exp(-10*dt);camX+=(x-camX)*a;camY+=(y-camY)*a;}
function start(){if(!ready)return;game.reset(loadout);savedResult=false;phase='game';stopStick();camera(true);sync();}
function home(){stopStick();phase='home';sync();}
function camp(){stopStick();phase='camp';sync();}
function showModal(title,info,labels){
 stopStick();phase='modal';Ouse.ui.setText('title',title);Ouse.ui.setText('info',info);
 for(let i=0;i<3;i++){Ouse.ui.setText('choice'+i,labels[i]||'');labels[i]?Ouse.ui.show('choice'+i):Ouse.ui.hide('choice'+i);}sync();
}
function pause(){if(phase!=='game')return;game.pause();showModal('生存暂停','', ['继续战斗','整备营地','返回主菜单']);}
function settings(){showModal('设置','',[preferences.hand==='left'?'摇杆：左手':'摇杆：右手',preferences.effects?'特效：开启':'特效：关闭','返回主菜单']);}
function choose(i){
 const s=game.state;
 if(Ouse.ui.getText('title')==='设置'){
  if(i===2){home();return;}if(i===0)preferences.hand=preferences.hand==='left'?'right':'left';else preferences.effects=!preferences.effects;persist();layout();settings();return;
 }
 if(s.mode==='paused'){if(i===0){game.resume();phase='game';last=Date.now();sync();}else if(i===1)camp();else home();return;}
 if(s.mode==='upgrade'){if(game.choose(i)){phase='game';last=Date.now();sync();}return;}
 if(i===0)start();else if(i===1)camp();else home();
}
function requestScreen(){if(Ouse.display.get().orientation!=='landscape')Ouse.display.setOrientation('landscape');Ouse.display.setFullscreen(true);}
function layout(){
 const d=Ouse.display.get();if(d.orientation!=='landscape'){requestScreen();return;}
 width=Math.floor(d.contentWidthDp);height=Math.floor(d.contentHeightDp);
 const cw=Math.floor((width-166)/2),row=Math.floor((height-116)/3),mw=Math.min(360,width-24),mh=Math.min(238,height-24),right=width-184;
 Ouse.ui.layout([
  {id:'scene',x:0,y:0,width,height},{id:'hud',x:0,y:0,width,height},{id:'home',x:0,y:0,width,height},{id:'camp',x:0,y:0,width,height},
  {id:'menuTitle',x:26,y:22,width:260,height:48},{id:'menuStart',x:right,y:Math.round(height*.34),width:164,height:44},{id:'menuCamp',x:right,y:Math.round(height*.34)+54,width:164,height:42},{id:'menuExit',x:right,y:Math.round(height*.34)+106,width:164,height:38},{id:'settings',x:width-76,y:14,width:60,height:32},
  ...[0,1,2,3].map(i=>({id:'tab'+i,x:12+i*Math.floor((width-24)/4),y:14,width:Math.floor((width-24)/4)-8,height:34})),
  ...[0,1,2,3,4,5].flatMap(i=>[{id:'equip'+i,x:144+i%2*(cw+10),y:64+Math.floor(i/2)*row,width:cw,height:row-6},{id:'equipName'+i,x:46,y:Math.max(2,Math.floor((row-34)/2)),width:cw-50,height:28}]),
  {id:'previewName',x:14,y:height>=280?158:134,width:116,height:28},{id:'previewInfo',x:14,y:height>=280?194:164,width:116,height:height>=280?66:28},
  {id:'campStart',x:width-150,y:height-44,width:136,height:34},{id:'campBack',x:14,y:height-44,width:100,height:34},
  {id:'joy',x:preferences.hand==='left'?16:width-112,y:height-108,width:96,height:96},{id:'skill',x:preferences.hand==='left'?width-84:20,y:height-88,width:64,height:64},{id:'pause',x:width-62,y:10,width:48,height:38},
  {id:'modal',x:Math.floor((width-mw)/2),y:Math.floor((height-mh)/2),width:mw,height:mh},{id:'title',x:16,y:10,width:mw-32,height:32},{id:'info',x:16,y:46,width:mw-32,height:26},
  ...[0,1,2].map(i=>({id:'choice'+i,x:16,y:80+i*44,width:mw-32,height:38}))
 ]);
 stopStick();camera(true);sync();
}
function render(){
 if(!active||closed)return;
 const s=game.state,h=s.hero,out=[];
 const rect=(x,y,w,hh,color,radius=0,opacity=255)=>out.push({op:'fillRect',x:Math.round(x),y:Math.round(y),width:Math.max(1,Math.round(w)),height:Math.max(1,Math.round(hh)),color,radius,opacity});
 const line=(x,y,x2,y2,color,lineWidth=2,opacity=255)=>out.push({op:'drawLine',x:Math.round(x),y:Math.round(y),x2:Math.round(x2),y2:Math.round(y2),color,lineWidth,opacity});
 function img(src,f,x,y,size=24){const cols=src==='actors'?13:8;out.push({op:'drawImage',src,x:Math.round(x),y:Math.round(y),width:Math.round(size),height:Math.round(size),sourceX:f%cols*24,sourceY:Math.floor(f/cols)*24,sourceWidth:24,sourceHeight:24});}
 const extra=(f,x,y,size=24)=>img('extra',f,x,y,size),world=(x,y)=>x>camX-50&&x<camX+width+50&&y>camY-50&&y<camY+height+50;
 function campScene(){
  rect(0,0,width,height,0x173c3a);rect(0,height*.52,width,height*.48,0x294b3c);
  for(let i=0;i<8;i++){const x=i*86-clock*(i%2?2:1)%86;extra(23,x-24,height*.28+i%3*20,92);}
  rect(0,height-38,width,38,0x345740);for(let i=0;i<10;i++)rect((i*59+Math.sin(clock*.7+i)*10)%width,54+i%4*35+Math.cos(clock+i)*8,3,3,0xe7d58a,1,150);
 }
 if(phase==='home'||phase==='camp'||(phase==='modal'&&Ouse.ui.getText('title')==='设置')){
  campScene();
  if(phase==='camp'){
   rect(6,56,128,height-108,0x16312e,18,210);
   extra(loadout.hero*2+Math.floor(clock*5)%2,34,70,76);extra(8+loadout.pet*2+Math.floor(clock*4)%2,91,114,36);extra(16+loadout.gun,87,91,40);
   const cw=Math.floor((width-166)/2),row=Math.floor((height-116)/3);
   for(let i=0;i<lists[tab].length;i++){
    const x=144+i%2*(cw+10),y=64+Math.floor(i/2)*row,on=loadout[tabKeys[tab]]===i;
    rect(x,y,cw,row-6,on?0xd6cf9b:0x35594b,12);extra(tab===0?i*2:tab===1?16+i:tab===2?8+i*2:22+i,x+6,y+Math.max(0,(row-42)/2),36);
   }
  }else{
   const ground=height-42;
   extra(24,26,ground-48,54);extra(loadout.hero*2+Math.floor(clock*5)%2,88,ground-102,106);extra(8+loadout.pet*2+Math.floor(clock*4)%2,184,ground-42+Math.sin(clock*3)*3,48);
   rect(52,ground-8,58,20,0xf3b35a,10,40);rect(69,ground-28-Math.sin(clock*8)*3,13,28,0xe88d42,4);rect(73,ground-21,7,20,0xffd67e,3);
  }
 }else{
  const colors=[0x365747,0x455454,0x4e647b],tile=[0x3b5f4d,0x4c5b5b,0x566e83];rect(0,0,width,height,colors[s.loadout.zone]);
  for(let y=-camY%80;y<height;y+=80)for(let x=-camX%80;x<width;x+=80)rect(x+8,y+14,3,8,tile[s.loadout.zone],1);
  if(s.loadout.zone)for(const p of s.hazards)if(world(p.x,p.y))rect(p.x-camX-p.r,p.y-camY-p.r,p.r*2,p.r*2,s.loadout.zone===1?0x8cbd62:0xb4d7dc,p.r,105);
  for(let i=0;i<42;i++){const x=(i*211+130)%s.width,y=(i*157+90)%s.height;if(world(x,y))extra(s.loadout.zone===2?26:22+i%4,x-camX-18,y-camY-18,36);}
  for(const g of s.gems)if(g.on&&world(g.x,g.y))img('actors',9,g.x-camX-10,g.y-camY-10,20);
  for(const f of s.foes)if(f.on&&world(f.x,f.y)){
   const size=f.boss?60:f.type===2?38:30,x=f.x-camX-size/2,y=f.y-camY-size/2;
   if(f.boss||f.type===3)extra(f.boss?31:30,x,y,size);else img('actors',f.type===2?6:f.type===1?51:4,x,y,size);
   if(f.boss||f.hp<f.maxHp){rect(x,y-6,size,3,0x273a40);rect(x,y-6,size*Math.max(0,f.hp/f.maxHp),3,f.boss?0xf1bc6b:0xe39373);}
   if(f.charge>.28)out.push({op:'strokeRect',x:Math.round(x-3),y:Math.round(y-3),width:size+6,height:size+6,radius:12,lineWidth:2,color:0xf6bd6c});
  }
  for(const b of s.shots)if(b.on&&world(b.x,b.y)){const c=[0xf2da8c,0xe7b576,0xf3d992,0xa6dce7,0xeab767][b.gun];line(b.x-camX,b.y-camY,b.x-camX-b.dx*.018,b.y-camY-b.dy*.018,c,b.gun===4?6:b.gun===3?3:2);}
  for(const b of s.enemyShots)if(b.on&&world(b.x,b.y))rect(b.x-camX-4,b.y-camY-4,8,8,0xb9df8e,4);
  if(preferences.effects)for(const f of s.effects)if(f.on&&world(f.x,f.y)){
   if(f.kind==='arc')line(f.x-camX,f.y-camY,f.x2-camX,f.y2-camY,0xa7e4eb,3,190);
   else out.push({op:'strokeRect',x:Math.round(f.x-camX-f.r),y:Math.round(f.y-camY-f.r),width:f.r*2,height:f.r*2,radius:f.r,lineWidth:3,color:f.kind==='heal'?0xb8ddb0:0xf1bc6b,opacity:Math.round(f.life/.25*200)});
  }
  if(s.turret.time>0)extra(28,s.turret.x-camX-16,s.turret.y-camY-16,32);
  for(let i=0;i<Math.min(2,s.orbit);i++){const a=s.elapsed*3+i*Math.PI;img('actors',50,h.x-camX+Math.cos(a)*45-12,h.y-camY+Math.sin(a)*45-12);}
  extra(8+s.loadout.pet*2+Math.floor(s.elapsed*6)%2,s.pet.x-camX-15,s.pet.y-camY-15,30);
  rect(h.x-camX-12,h.y-camY+11,24,8,0x1c3433,4,100);
  if(h.invincible<=0||Math.floor(h.invincible*14)%2===0){extra(s.loadout.hero*2+(held?Math.floor(s.elapsed*8)%2:0),h.x-camX-18,h.y-camY-20,36);img('actors',16+((Math.round(Math.atan2(h.dy,h.dx)*32/(Math.PI*2))+32)%32),h.x-camX+h.dx*11-12,h.y-camY+h.dy*11-12);}
 }
 if(phase==='modal')rect(0,0,width,height,0x0c2425,0,150);
 if(out.length>256)throw Error('Survivor scene budget exceeded');Ouse.canvas.submit('scene',out);
}
function renderHud(){
 const s=game.state,h=s.hero,key=[loop.fps,phase,Math.ceil(h.hp),s.hero.maxHp,s.wave,Math.ceil(s.waveTime),s.kills,s.level,s.xp,s.gunRank,Math.ceil(s.skillCooldown*10),joyX,joyY,held,width,height].join(':');if(key===hudKey)return;hudKey=key;
 const out=[];if(phase==='game'){
  out.push({op:'drawText',x:width-94,y:73,width:90,height:24,text:(loop.fps?loop.fps+' FPS':'-- FPS'),color:0xf7edd0});
  const rect=(x,y,width,height,color,radius=0,opacity=255)=>out.push({op:'fillRect',x:Math.round(x),y:Math.round(y),width:Math.max(1,Math.round(width)),height:Math.max(1,Math.round(height)),color,radius,opacity});
  const text=(x,y,width,value)=>out.push({op:'drawText',x,y,width,height:24,text:value,color:0xf7edd0});
  rect(16,15,130,12,0x203c3b,6,180);rect(16,15,130*h.hp/h.maxHp,12,0xc5cf8b,6);rect(16,33,130,3,0x253d3d);rect(16,33,130*s.xp/s.nextXp,3,0xe6b467);
  text(163,10,170,'第 '+s.wave+'/8 波  '+Math.ceil(s.waveTime)+'秒');text(width-144,43,138,'击杀 '+s.kills);
  text(16,43,250,content.guns[s.loadout.gun].name+' '+s.gunRank+'级');
  const x=preferences.hand==='left'?16:width-112,y=height-108,sx=preferences.hand==='left'?width-84:20;
  out.push({op:'strokeRect',x,y,width:96,height:96,radius:48,lineWidth:2,color:0xe5dfbd,opacity:130});rect(x+32+joyX,y+32+joyY,32,32,0xd9d8b1,16,held?180:95);
  out.push({op:'strokeRect',x:sx,y:height-88,width:64,height:64,radius:32,lineWidth:2,color:0xf1ce8e,opacity:180});
  rect(sx+26,height-74,12,34,0xefd096,3,s.skillCooldown>0?60:230);rect(sx+15,height-63,34,12,0xefd096,3,s.skillCooldown>0?60:230);
  if(s.skillCooldown>0)out.push({op:'drawArc',x:sx+32,y:height-56,radius:29,startAngle:0,endAngle:Math.max(1,Math.round(360*s.skillCooldown/content.heroes[s.loadout.hero].cooldown)),lineWidth:3,color:0xd6e5bf});
  rect(width-48,19,6,20,0xf1e5bf,1);rect(width-36,19,6,20,0xf1e5bf,1);
 }Ouse.canvas.submit('hud',out);
}
function tick(){
 if(closed||!active)return;const now=Date.now(),dt=Math.max(0,Math.min(.125,(now-last)/1000));last=now;clock+=dt;sound.update(now);
 if(phase==='game'){
  game.advance(dt);camera(false,dt);
  for(const event of game.drainEvents()){
   if(event==='shot'){if(now-shotAt>=120){sound.effect('shot');shotAt=now;}}else sound.effect(event);
  }
  const s=game.state;
  if(now>=reportAt){reportAt=now+5000;console.log('SURVIVOR_STATE '+JSON.stringify({x:Math.round(s.hero.x),y:Math.round(s.hero.y),kills:s.kills,wave:s.wave,hero:s.loadout.hero,gun:s.loadout.gun,pet:s.loadout.pet,zone:s.loadout.zone,petKills:s.petKills,mode:s.mode,fps:loop.fps}));}
  if(s.mode==='upgrade')showModal(s.upgradeReason==='wave'?'波次完成':'角色升级','第 '+s.wave+' 波 · 等级 '+s.level,s.choices.map(key=>upgradeLabels[key]+(key==='weapon'?' '+(s.gunRank+1)+'级':'')));
  if(s.mode==='dead'||s.mode==='won'){
   if(!savedResult){savedResult=true;best=Math.max(best,s.kills);Ouse.storage.set('best',best).catch(()=>{});}
   showModal(s.mode==='won'?'生存成功':'挑战结束','击杀 '+s.kills+' · 伙伴击杀 '+s.petKills+' · 最佳 '+best,['再来一局','整备营地','返回主菜单']);
  }
 }
 render();renderHud();
}
function joystick(e){if(phase!=='game')return;const dx=e.x-48,dy=e.y-48,d=Math.hypot(dx,dy);held=true;joyX=d<6?0:Math.round(dx/Math.max(36,d)*29);joyY=d<6?0:Math.round(dy/Math.max(36,d)*29);game.move(joyX/29,joyY/29);hudKey='';}
for(let i=0;i<3;i++)Ouse.ui.on('choice'+i,'click',()=>choose(i));
for(let i=0;i<4;i++)Ouse.ui.on('tab'+i,'click',()=>{tab=i;sync();});
for(let i=0;i<6;i++)Ouse.ui.on('equip'+i,'click',()=>{if(i>=lists[tab].length)return;loadout[tabKeys[tab]]=i;persist();sync();});
Ouse.ui.on('menuStart','click',start);Ouse.ui.on('menuCamp','click',camp);Ouse.ui.on('menuExit','click',()=>Ouse.home());Ouse.ui.on('settings','click',settings);Ouse.ui.on('campStart','click',start);Ouse.ui.on('campBack','click',home);
Ouse.ui.on('pause','click',pause);Ouse.ui.on('skill','click',()=>{if(game.skill())sound.effect('skill');});
Ouse.ui.on('joy','pointerdown',joystick);Ouse.ui.on('joy','pointermove',joystick);for(const e of ['pointerup','pointercancel'])Ouse.ui.on('joy',e,stopStick);
Ouse.display.onChange(layout);
globalThis.app={onPause(){stopStick();game.pause();sound.sync(false);active=false;loop.stop();},onResume(){active=true;requestScreen();if(phase==='game')pause();layout();last=Date.now();loop.start();},onClose(){sound.close();closed=true;active=false;loop.stop();stopStick();}};
Promise.all(['loadout-v2','settings','best'].map(key=>Ouse.storage.get(key).catch(()=>null))).then(values=>{
 if(closed)return;
 if(values[0])for(let i=0;i<4;i++)loadout[tabKeys[i]]=clamp(values[0][tabKeys[i]]|0,0,lists[i].length-1);
 if(values[1])preferences={hand:values[1].hand==='right'?'right':'left',effects:values[1].effects!==false};
 if(Number.isFinite(values[2]))best=Math.max(0,values[2]|0);ready=true;if(active)layout();
});
requestScreen();layout();last=Date.now();loop.start();
