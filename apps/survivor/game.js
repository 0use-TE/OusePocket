const game=createSurvivorEngine(),content=game.content,sound=createSurvivorAudio(Ouse),loop=createGameRuntime(tick);
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
  if(now>=reportAt){reportAt=now+5000;console.log('SURVIVOR_STATE '+JSON.stringify({x:Math.round(s.hero.x),y:Math.round(s.hero.y),kills:s.kills,wave:s.wave,hero:s.loadout.hero,gun:s.loadout.gun,pet:s.loadout.pet,zone:s.loadout.zone,petKills:s.petKills,mode:s.mode}));}
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
