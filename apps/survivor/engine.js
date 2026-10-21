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
  for(const b of s.shots)if(b.on){
   const x=b.x,y=b.y;b.x+=b.dx*dt;b.y+=b.dy*dt;b.life-=dt;
   for(const f of s.foes)if(f.on&&!(b.mask&(1<<f.id))&&segmentDistance(f,x,y,b.x,b.y)<(f.radius+3)**2){
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
 reset();return {reset,move,advance,skill,choose,pause,resume,spawn,hit,hurt,blast,drop,drainEvents(){const result=events;events=[];return result;},get state(){return s;},content:SurvivorContent};
}
