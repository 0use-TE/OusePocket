"use strict";
const IDLE = 0, WAIT = 1, GO = 2, DONE = 3, FOUL = 4;
let state = IDLE;
let best = 0;
let waitTimer = 0;
let goAt = 0;

function text(id, value) { Ouse.ui.setText(id, String(value)); }
function sfx(id) {
  try { if (Ouse.media.play({ asset: id })) return; } catch (_) {}
  try { Ouse.audio.testTone(); } catch (_) {}
}

function clearWait() {
  if (waitTimer) { Ouse.timers.clear(waitTimer); waitTimer = 0; }
}

function showBest() {
  text("best", best ? ("最佳 " + best + " 毫秒") : "最佳 --");
}

function setPad(label, bg, fg) {
  text("pad", label);
  Ouse.ui.setStyle("pad", { backgroundColor: bg, textColor: fg || "$text" });
}

function arm() {
  clearWait();
  state = WAIT;
  goAt = 0;
  text("result", "");
  setPad("等变色", "$error", "#ffffff");
  const delay = 1000 + ((Math.random() * 2500) | 0);
  waitTimer = Ouse.timers.setTimeout(function () {
    waitTimer = 0;
    if (state !== WAIT) return;
    state = GO;
    goAt = Ouse.system.info().uptimeMs;
    setPad("点!", "$accent", "#ffffff");
    sfx("go");
  }, delay);
}

function onPad() {
  if (state === IDLE || state === DONE || state === FOUL) {
    arm();
    return;
  }
  if (state === WAIT) {
    clearWait();
    state = FOUL;
    setPad("太早了", "$surface", "$error");
    text("result", "抢跑了");
    sfx("foul");
    return;
  }
  if (state === GO) {
    const ms = Math.max(0, Ouse.system.info().uptimeMs - goAt);
    state = DONE;
    if (!best || ms < best) best = ms;
    setPad(ms + " 毫秒", "$surface", "$text");
    text("result", "再点继续");
    showBest();
    sfx("win");
    persist();
  }
}

function restart() {
  clearWait();
  state = IDLE;
  goAt = 0;
  setPad("点我开始", "$surface", "$text");
  text("result", "");
  showBest();
}

function persist() {
  Ouse.storage.set("reaction", { version: 1, best: best });
}

Ouse.ui.on("pad", "click", onPad);
Ouse.ui.on("restart", "click", restart);
restart();
Ouse.storage.get("reaction").then(function (value) {
  if (value && value.version === 1) {
    best = Number(value.best) | 0;
    showBest();
  }
}, function () {});

// BEGIN responsive simple game
// Embedded source: preserve game state and lifecycle while rearranging controls.
function installSimpleGameLayout(kind){
 function resize(){
  const d=Ouse.display.get();if(d.keyboardVisible)return;
  const w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),land=w>h,a=[];
  function f(id,x,y,width,height){a.push({id,x,y,width,height});}
  if(kind==='dice'){
   f('header',land?264:8,8,land?w-272:w-16,40);f('stage',8,land?8:54,land?240:w-16,land?h-16:h-172);
   f('sum',land?264:8,land?64:h-108,land?w-272:w-16,36);f('roll',land?264:8,h-64,land?w-272:w-16,56);
  }else if(kind==='rps'){
   f('heading',land?268:8,8,land?w-276:w-16,40);f('score',land?268:8,land?56:54,land?w-276:w-16,28);
   f('stage',8,land?8:88,land?244:w-16,land?h-16:h-160);f('actions',land?268:8,land?96:h-64,land?w-276:w-16,56);
  }else if(kind==='reaction'){
   f('heading',8,8,land?184:w-16,40);f('best',8,54,land?184:w-16,28);f('result',8,h-48,land?184:w-16,40);
   f('pad',land?208:8,land?8:88,land?w-216:w-16,land?h-16:h-144);
  }else{
   const sw=w-16,column=land?Math.floor(sw/2)-16:sw-16,right=land?Math.floor(sw/2)+8:8;
   f('heading',8,8,w-16,40);f('stage',8,56,sw,h-64);
   f('range',8,8,column,36);f('hint',8,52,column,48);f('tries',8,108,column,32);f('spacer',0,0,1,1);
   f('input',right,land?24:164,column,48);f('guess',right,land?88:220,column,56);
  }
  Ouse.ui.layout(a);
 }
 Ouse.display.onChange(resize);const app=globalThis.app||{},resume=app.onResume;
 app.onResume=function(){if(resume)resume();resize();};globalThis.app=app;resize();
}

installSimpleGameLayout('reaction');
// END responsive simple game
