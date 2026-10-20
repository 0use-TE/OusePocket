"use strict";
// Bounded pools: no LVGL objects or timers are created during combat.
const ENEMIES = 12, SHOTS = 6, GEMS = 8, WAVES = 8, WAVE_SECONDS = 20;
const foes = Array.from({ length: ENEMIES }, (_, i) => ({ id: 'e' + i, on: false, x: 0, y: 0, hp: 0, type: 0 }));
const shots = Array.from({ length: SHOTS }, (_, i) => ({ id: 'b' + i, on: false, x: 0, y: 0, dx: 0, dy: 0, life: 0 }));
const gems = Array.from({ length: GEMS }, (_, i) => ({ id: 'xp' + i, on: false, x: 0, y: 0, value: 0 }));
const rockets = Array.from({length:1},(_,i)=>({id:'rocket'+i,on:false,x:0,y:0,dx:0,dy:0,life:0,target:null}));
const blasts = Array.from({length:1},(_,i)=>({id:'blast'+i,on:false,x:0,y:0,radius:0,life:0,stage:-1}));
const swords = Array.from({length:2},(_,i)=>({id:'sword'+i,on:false,x:0,y:0}));
const pools = [foes,shots,gems,rockets,blasts,swords];
let rocketRank=0,orbitRank=0,rangeScale=1,armor=0,regen=0,rocketClock=0,spawnCount=0;
let width = 480, height = 256, fieldH = 222, phase = 'intro', timer = 0, last = 0, closed = false;
let px = 240, py = 110, mx = 0, my = 0, held = false, joyX = 0, joyY = 0;
let hp = 80, maxHp = 80, damage = 12, rate = .48, speed = 86, magnet = 32, level = 1, xp = 0, needed = 7;
let wave = 1, seconds = 20, kills = 0, elapsed = 0, spawnClock = 0, fireClock = 0, invincible = 0, upgradePoints = 0;
let aimDX = 1, aimDY = 0;
let best = 0, hudClock = 0, choiceKeys = [], lastHud = '', maxFrameMs = 0, frameCount = 0, frameTotal = 0, metricsClock = 0;
const shown = {}, drawn = {};
let preferences={hand:'left',effects:true},settingsTouched=false,preferencesReady=false,bestReady=false,introTime=0,menuTime=0;
const upgrades = {
 damage:'攻击 +30%', rate:'攻速 +20%', health:'生命 +20 · 恢复生命',
 speed:'移动 +12%', magnet:'拾取范围 +25%', rocket:'火箭弹 · 爆炸攻击',
 orbit:'环绕飞剑 · 数量增加', range:'范围 +20% · 飞剑与爆炸',
 armor:'护甲 +2', regen:'恢复 · 每秒回血'
};
const upgradeDeck=[['rocket','orbit','damage'],['range','health','rate'],['orbit','rocket','speed'],['rate','armor','regen'],['magnet','range','rocket'],['orbit','health','damage'],['damage','rate','range']];

function visible(id, on) { if (shown[id] !== on) { shown[id] = on; if (on) Ouse.ui.show(id); else Ouse.ui.hide(id); } }
function batch(patches) { for (let i = 0; i < patches.length; i += 32)Ouse.sprites.update(patches.slice(i, i + 32)); }
function stopStick(update = true) { held = false; mx = my = joyX = joyY = 0; if (update) Ouse.ui.layout([{ id: 'knob', x: 36, y: 36, width: 36, height: 36 }]); }
function layout() {
    const d = Ouse.display.get();
    if (d.orientation !== 'landscape') { stopLoop(); stopStick(); Ouse.display.setOrientation('landscape'); return; }
    width = Math.floor(d.contentWidthDp); height = Math.floor(d.contentHeightDp); fieldH = height - 34;
    const mw = Math.min(320, width - 24), mh = Math.min(224, height - 12), modalX = Math.floor((width - mw) / 2), modalY = Math.floor((height - mh) / 2);
    Ouse.ui.layout([{ id: 'field', x: 0, y: 34, width, height: fieldH }, { id: 'hp', x: 12, y: 2, width: 120, height: 22 }, { id: 'hpTrack', x: 12, y: 27, width: 112, height: 3 }, { id: 'wave', x: 144, y: 2, width: 120, height: 24 }, { id: 'time', x: 268, y: 2, width: 54, height: 24 }, { id: 'kills', x: width - 150, y: 2, width: 80, height: 24 }, { id: 'pause', x: width - 66, y: 2, width: 60, height: 28 }, {id:'loadout',x:136,y:height-26,width:width-244,height:22}, { id: 'xpLabel', x: width - 100, y: height - 26, width: 94, height: 22 }, { id: 'joy', x: preferences.hand==='left'?14:width-122, y: height - 116, width: 108, height: 108 }, { id: 'knob', x: 36, y: 36, width: 36, height: 36 }, { id: 'modal', x: modalX, y: modalY, width: mw, height: mh }, { id: 'title', x: 16, y: 10, width: mw - 32, height: 32 }, { id: 'info', x: 16, y: 45, width: mw - 32, height: 40 }, { id: 'choice0', x: 16, y: 88, width: mw - 32, height: 36 }, { id: 'choice1', x: 16, y: 130, width: mw - 32, height: 36 }, { id: 'choice2', x: 16, y: 172, width: mw - 32, height: 36 }]);
    px = Math.max(18, Math.min(width - 18, px)); py = Math.max(18, Math.min(fieldH - 18, py));
    const rocks = []; for (let i = 0; i < 2; i++)rocks.push({ id: 'rock' + i, x: Math.floor(width * (.16 + (i % 3) * .29)), y: Math.floor(fieldH * (i < 3 ? .2 : .72)) }); batch(rocks);
    for (const pool of pools) for (const p of pool) { p.x = Math.max(12, Math.min(width - 12, p.x)); p.y = Math.max(12, Math.min(fieldH - 12, p.y)); }
    stopStick(); layoutMenu(); if(phase==='intro'){layoutIntro(Math.min(1,introTime/1.25));showGameplay(false);startLoop();} else if(phase==='running'||phase==='pause'||phase==='upgrade'||phase==='dead'||phase==='won'){render();hud(true);if(phase==='running')startLoop();}
}
function modal(title, info, labels) {
    stopStick(); Ouse.ui.setText('title', title); Ouse.ui.setText('info', info);
    for (let i = 0; i < 3; i++) { Ouse.ui.setText('choice' + i, labels[i] || ''); visible('choice' + i, !!labels[i]); }
    visible('modal', true); visible('joy', false); visible('pause', false);
}
function start() {
    phase = 'running'; hp = maxHp = 80; damage = 12; rate = .48; speed = 86; magnet = 32; level = 1; xp = 0; needed = 7; wave = 1; seconds = 20; kills = elapsed = 0; spawnClock = 0; fireClock = 0; invincible = 0; upgradePoints = 0; maxFrameMs = frameCount = frameTotal = metricsClock = 0;
    rocketRank=0;orbitRank=0;rangeScale=1;armor=regen=0;rocketClock=.6;spawnCount=0;aimDX=1;aimDY=0;
    px = width * .52; py = fieldH * .5; for (const pool of pools) for (const p of pool) p.on = false;
    Ouse.ui.layout([{id:'joy',x:preferences.hand==='left'?14:width-122,y:height-116,width:108,height:108}]);
    visible('menuScene',false);visible('intro',false);showGameplay(true);startLoop();
    visible('modal', false); visible('joy', true); visible('pause', true); visible('hero', true); visible('gun', true); stopStick(); hud(true); render();
}
function gain(value) { xp += value; while (xp >= needed) { xp -= needed; level++; upgradePoints++; damage += 2; maxHp += 3; hp = Math.min(maxHp, hp + 3); needed = Math.floor(needed * 1.25 + 3); } }
function saveBest() { if (kills <= best) return; best = kills; Ouse.storage.set('best', best).catch(() => { }); }
function finish(win) { phase = win ? 'won' : 'dead'; saveBest(); hud(true); modal(win ? '生存成功！' : '本次挑战结束', '击杀 ' + kills + '  ·  到达第 ' + wave + ' 波\n最佳 ' + best, ['再来一局', '返回主菜单', '']); }
function openUpgrades() {
 phase='upgrade';const deck=upgradeDeck[(wave-1)%upgradeDeck.length];
 choiceKeys=deck.map(key=>[key,upgrades[key]+(key==='rocket'?'  Lv.'+(rocketRank+1):key==='orbit'?'  Lv.'+(orbitRank+1):'')]);
 modal('选择强化','第 '+wave+' 波完成 · LV '+level,choiceKeys.map(v=>v[1]));
}

function choose(index) {
    if (phase === 'settings') { if (index === 0) preferences.hand = preferences.hand === 'left' ? 'right' : 'left'; else if(index === 1) preferences.effects = !preferences.effects; else { menu(); return; } settingsTouched = true; savePreferences(); settings(); return; }
    if (phase === 'pause') { if (index === 0) { phase = 'running'; visible('modal', false); visible('joy', true); visible('pause', true); last = Date.now(); startLoop(); } else if (index === 1) menu(); return; }
    if (phase === 'dead' || phase === 'won') { if (index === 0) start(); else menu(); return; }
    if (phase !== 'upgrade') return;
    const kind = choiceKeys[index][0];
    if(kind==='rocket')rocketRank=Math.min(3,rocketRank+1);
    if(kind==='orbit')orbitRank=Math.min(3,orbitRank+1);
    if(kind==='range')rangeScale=Math.min(1.4,rangeScale*1.15);
    if(kind==='armor')armor=Math.min(10,armor+2);
    if(kind==='regen')regen=Math.min(4,regen+1);
    if (kind === 'damage') damage = Math.round(damage * 1.3); if (kind === 'rate') rate = Math.max(.12, rate / 1.2);
    if (kind === 'health') { maxHp += 20; hp = Math.min(maxHp, hp + 45); } if (kind === 'speed') speed = Math.min(155, speed * 1.12); if (kind === 'magnet') magnet = Math.min(96, magnet * 1.25);
    if (upgradePoints > 0) upgradePoints--;
    wave++; seconds = 20; hp = Math.min(maxHp, hp + 8); phase = 'running'; spawnClock = 0; fireClock = 0; visible('modal', false); visible('joy', true); visible('pause', true); hud(true);
}
function showGameplay(on) {
 for(const id of ['field','hp','hpTrack','wave','time','kills','xpLabel','loadout'])visible(id,on);
 visible('joy',on&&phase==='running');visible('pause',on&&phase==='running');
}
function menu() {
 phase='menu';stopLoop();stopStick();for(const pool of pools)for(const p of pool)p.on=false;
 visible('hero',false);visible('gun',false);visible('modal',false);visible('intro',false);showGameplay(false);visible('menuScene',true);
 Ouse.ui.setText('menuVersion','V1.0.0  ·  最佳击杀 '+best);
 for(const id of ['menuStart','menuSettings','menuExit'])Ouse.ui.setEnabled(id,true);
}
function settings() {
 phase='settings';visible('menuScene',false);showGameplay(false);
 modal('设置','按你的习惯出发',[preferences.hand==='left'?'摇杆：左手':'摇杆：右手',preferences.effects?'爆炸特效：开':'爆炸特效：关','返回主菜单']);
}
function savePreferences(){Ouse.storage.set('settings',{hand:preferences.hand,effects:preferences.effects}).catch(()=>{});}
function exitGame(){phase='exit';stopLoop();stopStick();if(!Ouse.home())menu();}
function beginIntro(){
 phase='intro';introTime=0;menuTime=0;showGameplay(false);visible('modal',false);visible('menuScene',false);visible('intro',true);
 for(const id of ['menuStart','menuSettings','menuExit'])Ouse.ui.setEnabled(id,false);
 if(Ouse.display.get().orientation==='landscape'){layoutIntro(0);startLoop();}
}
function layoutMenu(){
 const right=width-192;
 Ouse.ui.layout([{id:'menuScene',x:0,y:0,width,height},{id:'menuTag',x:24,y:18,width:220,height:24},{id:'menuTitle',x:24,y:43,width:220,height:44},{id:'menuHint',x:24,y:91,width:240,height:28},{id:'menuLine',x:right-24,y:26,width:2,height:height-52},{id:'menuRing',x:78,y:124,width:78,height:78},{id:'menuBadge',x:91,y:137,width:52,height:52},{id:'menuVersion',x:24,y:height-27,width:240,height:22},{id:'menuStart',x:right,y:48,width:176,height:44},{id:'menuSettings',x:right,y:104,width:176,height:44},{id:'menuExit',x:right,y:160,width:176,height:44},{id:'intro',x:0,y:0,width,height},{id:'introTitle',x:0,y:Math.round(height*.31)+44,width,height:40},{id:'introSubtitle',x:0,y:Math.round(height*.31)+86,width,height:22},{id:'introTrack',x:Math.round((width-200)/2),y:height-40,width:200,height:3},{id:'introCaption',x:Math.round((width-200)/2),y:height-30,width:200,height:22}]);
}
function layoutIntro(progress){
 const cx=Math.floor(width/2),cy=Math.round(height*.31),ease=1-Math.pow(1-Math.min(1,progress),3),radius=Math.round(48-7*ease),angle=progress*Math.PI*2-Math.PI/2;
 Ouse.ui.layout([{id:'introRing',x:cx-radius,y:cy-radius,width:radius*2,height:radius*2},{id:'introBadge',x:cx-26,y:cy-26+Math.round((1-ease)*7),width:52,height:52},{id:'introDot',x:Math.round(cx+Math.cos(angle)*radius)-3,y:Math.round(cy+Math.sin(angle)*radius)-3,width:6,height:6},{id:'introFill',x:0,y:0,width:Math.max(1,Math.round(200*ease)),height:3}]);
 Ouse.ui.setStyle('introRing',{radius});
}
function animateIntro(dt){
 introTime+=Math.min(dt,.1);layoutIntro(Math.min(1,introTime/1.25));
 if(introTime>=1.25&&(preferencesReady&&bestReady||introTime>=2.8)){
  menu();phase='menuEnter';menuTime=0;
  for(const id of ['menuStart','menuSettings','menuExit']){Ouse.ui.setEnabled(id,false);Ouse.ui.setStyle(id,{opacity:64});}startLoop();
 }
}
function animateMenu(dt){
 menuTime+=dt;const ids=['menuStart','menuSettings','menuExit'];
 for(let i=0;i<ids.length;i++)Ouse.ui.setStyle(ids[i],{opacity:Math.round(64+191*Math.max(0,Math.min(1,(menuTime-i*.07)/.18)))});
 if(menuTime>=.34){phase='menu';for(const id of ids){Ouse.ui.setEnabled(id,true);Ouse.ui.setStyle(id,{opacity:255});}stopLoop();}
}

function pause() { if (phase !== 'running') return; phase = 'pause'; modal('游戏暂停', '松开摇杆即可停止移动', ['继续游戏', '返回主菜单', '']); }
function joystick(e) { if (phase !== 'running') return; held = true; let dx = e.x - 54, dy = e.y - 54, dist = Math.sqrt(dx * dx + dy * dy); if (dist < 7) { mx = my = 0; joyX = joyY = 0; } else { let n = Math.max(34, dist); mx = dx / n; my = dy / n; joyX = Math.round(mx * 30); joyY = Math.round(my * 30); } Ouse.ui.layout([{ id: 'knob', x: 36 + joyX, y: 36 + joyY, width: 36, height: 36 }]); }
function spawn() {
 const p=foes.find(v=>!v.on);if(!p)return;
 const edge=Math.floor(Math.random()*4),t=Math.random();
 p.x=edge===0?14:edge===1?width-14:14+t*(width-28);p.y=edge===2?14:edge===3?fieldH-14:14+t*(fieldH-28);
 if(Math.hypot(p.x-px,p.y-py)<52){p.x=px<width/2?width-14:14;p.y=py<fieldH/2?fieldH-14:14;}
 spawnCount++;const r=Math.random();
 p.type=wave>=2&&r<.3?1:r>.75?2:0;
 const health=[15,34,10],velocity=[27,18,43];
 p.hp=health[p.type]+wave*3;p.speed=velocity[p.type]+Math.min(20,wave*2);p.orbitAt=0;p.on=true;
}

function drop(x, y, value) { let g = gems.find(v => !v.on); if (!g) { let bestDistance = Infinity; for (const p of gems) { let d = (p.x - x) ** 2 + (p.y - y) ** 2; if (d < bestDistance) { bestDistance = d; g = p; } } g.value += value; return; } g.on = true; g.x = x; g.y = y; g.value = value; }
function segmentDistance(x, y, ax, ay, bx, by) { const dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy, t = length ? Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / length)) : 0; return (x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2; }
function hurt(amount){
 if(invincible>0)return;hp=Math.max(0,hp-Math.max(1,amount-armor));invincible=.7;if(hp<=0)finish(false);
}
function hit(p,amount,area){
 if(!p.on)return;p.hp-=amount*(!area&&p.type===1?.7:1);
 if(p.hp<=0){p.on=false;kills++;drop(p.x,p.y,p.type===4?8:p.type===1?3:1);}
}
function explode(x,y){
 const radius=Math.min(48,(24+rocketRank*5)*rangeScale);
 const e=blasts.find(v=>!v.on)||blasts.reduce((a,b)=>a.life<b.life?a:b);e.on=true;e.x=x;e.y=y;e.radius=radius;e.life=.2;e.stage=-1;
 for(const p of foes)if(p.on&&Math.hypot(p.x-x,p.y-y)<=radius){hit(p,18+damage*.75+rocketRank*7,true);}
}

function update(dt) {
 if(phase!=='running')return;
 elapsed+=dt;seconds=Math.max(0,seconds-dt);invincible=Math.max(0,invincible-dt);hp=Math.min(maxHp,hp+regen*dt);
 px=Math.max(14,Math.min(width-14,px+mx*speed*dt));py=Math.max(14,Math.min(fieldH-14,py+my*speed*dt));
 spawnClock-=dt;if(spawnClock<=0){spawn();spawnClock=Math.max(.38,.9-wave*.055);}
 let target=null,near=Infinity;
 for(const p of foes)if(p.on){const dx=px-p.x,dy=py-p.y,d=Math.hypot(dx,dy)||1;p.x+=dx/d*p.speed*dt;p.y+=dy/d*p.speed*dt;
  if(d<18){hurt(p.type===1?15:9);if(phase!=='running')return;}if(d<near){target=p;near=d;}}
 if(target){const dx=target.x-px,dy=target.y-py,d=Math.hypot(dx,dy)||1;aimDX=dx/d;aimDY=dy/d;}
 fireClock-=dt;if(target&&fireClock<=0){const b=shots.find(v=>!v.on);if(b){b.on=true;b.x=px+aimDX*10;b.y=py+aimDY*10;b.dx=aimDX*245;b.dy=aimDY*245;b.life=1.6;fireClock=rate;}}
 for(const b of shots)if(b.on){const ax=b.x,ay=b.y;b.x+=b.dx*dt;b.y+=b.dy*dt;b.life-=dt;
  for(const p of foes)if(p.on&&segmentDistance(p.x,p.y,ax,ay,b.x,b.y)<(p.type===1?15:12)**2){hit(p,damage,false);b.on=false;break;}
  if(b.life<=0||b.x<0||b.x>width||b.y<0||b.y>fieldH)b.on=false;}
 rocketClock-=dt;if(rocketRank&&target&&rocketClock<=0){const r=rockets.find(v=>!v.on);if(r){r.on=true;r.x=px;r.y=py;r.dx=aimDX;r.dy=aimDY;r.target=target;r.life=2;rocketClock=Math.max(1.5,3.4-rocketRank*.4);}}
 for(const r of rockets)if(r.on){const ax=r.x,ay=r.y;if(r.target&&r.target.on){const dx=r.target.x-r.x,dy=r.target.y-r.y,d=Math.hypot(dx,dy)||1;r.dx=dx/d;r.dy=dy/d;}
  r.x+=r.dx*145*dt;r.y+=r.dy*145*dt;r.life-=dt;let impact=false;for(const p of foes)if(p.on&&segmentDistance(p.x,p.y,ax,ay,r.x,r.y)<169){impact=true;break;}
  if(impact||r.life<=0||r.x<4||r.x>width-4||r.y<4||r.y>fieldH-4){r.on=false;explode(Math.max(4,Math.min(width-4,r.x)),Math.max(4,Math.min(fieldH-4,r.y)));}}
 const radius=Math.min(48,(25+orbitRank*4)*rangeScale),count=Math.min(2,orbitRank);
 for(let i=0;i<swords.length;i++){const sword=swords[i];sword.on=i<count;if(!sword.on)continue;const a=elapsed*2.6+i*Math.PI*2/count;sword.x=px+Math.cos(a)*radius;sword.y=py+Math.sin(a)*radius;
  for(const p of foes)if(p.on&&elapsed>=(p.orbitAt||0)&&Math.hypot(p.x-sword.x,p.y-sword.y)<18){hit(p,10+orbitRank*5,true);p.orbitAt=elapsed+.45;}}
 for(const e of blasts)if(e.on){e.life-=dt;if(e.life<=0)e.on=false;}
 for(const g of gems)if(g.on){const dx=px-g.x,dy=py-g.y,d=Math.hypot(dx,dy);if(d<16){gain(g.value);g.on=false;}else if(d<magnet){g.x+=dx/d*130*dt;g.y+=dy/d*130*dt;}}
 if(seconds<=0){for(const g of gems)if(g.on)gain(g.value);for(const pool of pools)for(const p of pool)p.on=false;if(wave>=WAVES)finish(true);else openUpgrades();}
}

function gunFrame(dx, dy) { return 16 + ((Math.round(Math.atan2(dy, dx) * 32 / (Math.PI * 2)) + 32) % 32); }
function render() {
    const patches = [], frame = Math.floor(elapsed * 7) % 2;
    function actor(p, f) { visible(p.id, p.on); if (!p.on) return; const x = Math.round(p.x - 12), y = Math.round(p.y - 12), old = drawn[p.id]; if (!old || old.x !== x || old.y !== y || old.frame !== f) { patches.push({ id: p.id, x, y, frame: f }); drawn[p.id] = { x, y, frame: f }; } }
    for(const g of gems)actor(g,9);
    for(const p of foes)actor(p,p.type===2?51:(p.type===1?6:4));
    for(const b of shots)actor(b,8);
    for(const r of rockets)actor(r,48);
    for(const sword of swords)actor(sword,50);
    for(const e of blasts){visible(e.id,e.on&&preferences.effects);if(!e.on||!preferences.effects)continue;const stage=Math.min(2,Math.floor((.2-e.life)/.067));if(stage!==e.stage){e.stage=stage;const r=Math.min(30,Math.max(5,Math.round(e.radius*(.3+stage*.18))));Ouse.ui.layout([{id:e.id,x:Math.round(e.x-r),y:Math.round(e.y-r),width:r*2,height:r*2}]);Ouse.ui.setStyle(e.id,{radius:r,opacity:255,backgroundColor:['#ffe2a0','#ecac55','#92613d'][stage]});}}
    if (['running','pause','upgrade','dead','won'].includes(phase)) { const blink = invincible <= 0 || Math.floor(invincible * 12) % 2 === 0; actor({ id:'hero', on:blink, x:px, y:py }, held && (mx || my) ? frame : 0); actor({ id:'gun', on:blink, x:px+aimDX*11, y:py+aimDY*11 }, gunFrame(aimDX,aimDY)); }
    batch(patches);
}
function hud(force) {
    const text = Math.ceil(hp) + '/' + maxHp + '|' + wave + '|' + Math.ceil(seconds) + '|' + kills + '|' + level + '|' + xp;
    if (!force && text === lastHud) return; lastHud = text;
    Ouse.ui.setText('hp', 'HP ' + Math.ceil(hp) + ' / ' + maxHp); Ouse.ui.setText('wave', '第 ' + wave + ' / 8 波'); Ouse.ui.setText('time', Math.ceil(seconds) + 's'); Ouse.ui.setText('kills', '击杀 ' + kills); Ouse.ui.setText('xpLabel', 'LV ' + level + '  ' + xp + '/' + needed);
    Ouse.ui.setText('loadout', [rocketRank?'火箭'+rocketRank:'',orbitRank?'飞剑'+orbitRank:''].filter(Boolean).join('  ') || '自动机枪');
    Ouse.ui.layout([{ id: 'hpFill', x: 0, y: 0, width: Math.max(1, Math.floor(112 * hp / maxHp)), height: 3 }]);
}
function tick() {
    const now = Date.now(), delta = now - last; last = now; const dt = Math.max(0, Math.min(.08, delta / 1000));
    if(phase==='intro'){animateIntro(dt);return;}if(phase==='menuEnter'){animateMenu(dt);return;}
    if (phase === 'running') { maxFrameMs = Math.max(maxFrameMs, delta); frameTotal += delta; frameCount++; update(dt); render(); hudClock += dt; if (hudClock >= .2) { hudClock = 0; hud(false); } }
}
function startLoop() { if (timer || closed) return; last = Date.now(); timer = Ouse.timers.setInterval(tick, 50); }
function stopLoop() { if (timer) Ouse.timers.clear(timer); timer = 0; }
for (let i = 0; i < 3; i++)Ouse.ui.on('choice' + i, 'click', () => choose(i));
Ouse.ui.on('menuStart','click',()=>{if(phase==='menu')start();});Ouse.ui.on('menuSettings','click',()=>{if(phase==='menu')settings();});Ouse.ui.on('menuExit','click',()=>{if(phase==='menu')exitGame();});
Ouse.ui.on('pause', 'click', pause); Ouse.ui.on('joy', 'pointerdown', joystick); Ouse.ui.on('joy', 'pointermove', joystick); Ouse.ui.on('joy', 'pointerup', stopStick); Ouse.ui.on('joy', 'pointercancel', stopStick);
Ouse.display.onChange(layout);
globalThis.app = {
 onPause() { stopStick(false); if (phase === 'running') phase = 'pause'; stopLoop(); },
 onResume() { if(phase==='exit'){beginIntro();}layout(); if (phase === 'pause') {modal('游戏暂停','松开摇杆即可停止移动',['继续游戏','返回主菜单','']);} else if(phase==='intro'||phase==='menuEnter'||phase==='running')startLoop(); },
 onClose() { closed = true; stopStick(false); stopLoop(); }
};
Ouse.storage.get('best').then(v=>{if(closed)return;if(Number.isFinite(v)&&v>=0)best=Math.floor(v);if(phase==='menu')Ouse.ui.setText('menuVersion','V1.0.0  ·  最佳击杀 '+best);}).catch(()=>{}).then(()=>{bestReady=true;});
Ouse.storage.get('settings').then(v=>{if(closed||settingsTouched)return;if(v&&typeof v==='object'){preferences.hand=v.hand==='right'?'right':'left';preferences.effects=v.effects!==false;}}).catch(()=>{}).then(()=>{preferencesReady=true;});
beginIntro();layout();
