"use strict";
const NAMES = ["石头", "剪刀", "布"];
let win = 0, lose = 0, draw = 0;

function text(id, value) { Ouse.ui.setText(id, String(value)); }
function sfx(id) {
  try { if (Ouse.media.play({ asset: id })) return; } catch (_) {}
  try { Ouse.audio.testTone(); } catch (_) {}
}

function paintScore() {
  text("score", "胜 " + win + " 负 " + lose + " 平 " + draw);
}

function result(you, cpu) {
  if (you === cpu) return 0;
  if ((you === 0 && cpu === 1) || (you === 1 && cpu === 2) || (you === 2 && cpu === 0)) return 1;
  return -1;
}

function play(you) {
  const cpu = (Math.random() * 3) | 0;
  text("you", "你 " + NAMES[you]);
  text("cpu", "电脑 " + NAMES[cpu]);
  const r = result(you, cpu);
  if (r > 0) { win++; text("status", "你赢了"); sfx("win"); }
  else if (r < 0) { lose++; text("status", "你输了"); sfx("click"); }
  else { draw++; text("status", "平局"); sfx("click"); }
  paintScore();
  persist();
}

function restart() {
  win = lose = draw = 0;
  text("you", "你 ?");
  text("cpu", "电脑 ?");
  text("status", "选一个");
  paintScore();
  persist();
}

function persist() {
  Ouse.storage.set("rps", { version: 1, win: win, lose: lose, draw: draw });
}

Ouse.ui.on("rock", "click", function () { play(0); });
Ouse.ui.on("scissors", "click", function () { play(1); });
Ouse.ui.on("paper", "click", function () { play(2); });
Ouse.ui.on("restart", "click", restart);
paintScore();
text("status", "选一个");
Ouse.storage.get("rps").then(function (value) {
  if (value && value.version === 1) {
    win = Number(value.win) | 0;
    lose = Number(value.lose) | 0;
    draw = Number(value.draw) | 0;
    paintScore();
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

installSimpleGameLayout('rps');
// END responsive simple game
