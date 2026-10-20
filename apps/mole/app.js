"use strict";
const HOLES = 9;
const ROUND_MS = 30000;
const LEVELS = {
  easy: { label: "简单", waitMin: 320, waitMax: 900, stayMin: 520, stayMax: 980, multi: 0 },
  normal: { label: "普通", waitMin: 160, waitMax: 480, stayMin: 280, stayMax: 520, multi: 0 },
  hard: { label: "困难", waitMin: 70, waitMax: 240, stayMin: 160, stayMax: 340, multi: 0.55 }
};

let level = "normal";
let score = 0;
let best = { easy: 0, normal: 0, hard: 0 };
let running = false;
let leftMs = ROUND_MS;
let deadline = 0;
let tickId = 0;
let popId = 0;
let hideIds = {};
let hammerIds = {};
let page = "menu";

function text(id, value) { Ouse.ui.setText(id, String(value)); }

function sfx(id) {
  try {
    if (Ouse.media.play({ asset: id })) return;
  } catch (_) {}
  try { Ouse.audio.testTone(); } catch (_) {}
}

function clearTimers() {
  if (tickId) { Ouse.timers.clear(tickId); tickId = 0; }
  if (popId) { Ouse.timers.clear(popId); popId = 0; }
  const hides = hideIds;
  hideIds = {};
  for (const key in hides) {
    if (hides[key]) Ouse.timers.clear(hides[key]);
  }
  const hammers = hammerIds;
  hammerIds = {};
  for (const key in hammers) {
    if (hammers[key]) Ouse.timers.clear(hammers[key]);
  }
}

function showHole(i, kind) {
  Ouse.ui.setImage("m" + i, kind);
}

function clearBoard() {
  clearTimers();
  for (let i = 0; i < HOLES; i++) showHole(i, "hole");
}

function showMenuBest() {
  text("bestEasy", "最佳 " + best.easy);
  text("bestNormal", "最佳 " + best.normal);
  text("bestHard", "最佳 " + best.hard);
}

function showScore() {
  text("score", "分 " + score);
  text("best", "最佳 " + best[level]);
}

function showTime() {
  text("time", Math.max(0, Math.ceil(leftMs / 1000)) + "秒");
}

function isUp(i) {
  return !!hideIds[i];
}

function schedulePop() {
  if (!running) return;
  const cfg = LEVELS[level];
  const wait = cfg.waitMin + ((Math.random() * (cfg.waitMax - cfg.waitMin + 1)) | 0);
  popId = Ouse.timers.setTimeout(function () {
    popId = 0;
    if (!running) return;
    const open = [];
    for (let i = 0; i < HOLES; i++) if (!isUp(i) && !hammerIds[i]) open.push(i);
    if (!open.length) {
      schedulePop();
      return;
    }
    const count = (cfg.multi && Math.random() < cfg.multi && open.length > 1) ? 2 : 1;
    for (let n = 0; n < count; n++) {
      if (!open.length) break;
      const idx = (Math.random() * open.length) | 0;
      const pick = open.splice(idx, 1)[0];
      showHole(pick, "mole");
      const stay = cfg.stayMin + ((Math.random() * (cfg.stayMax - cfg.stayMin + 1)) | 0);
      hideIds[pick] = Ouse.timers.setTimeout(function () {
        hideIds[pick] = 0;
        delete hideIds[pick];
        if (!hammerIds[pick]) showHole(pick, "hole");
      }, stay);
    }
    schedulePop();
  }, wait);
}

function finish() {
  running = false;
  clearTimers();
  for (let i = 0; i < HOLES; i++) showHole(i, "hole");
  if (score > best[level]) best[level] = score;
  text("status", "结束 " + score + " 分");
  showScore();
  showTime();
  sfx("win");
  persist();
}

function tick() {
  leftMs = Math.max(0, deadline - Ouse.system.info().uptimeMs);
  showTime();
  if (leftMs <= 0) finish();
}

function start() {
  clearTimers();
  running = true;
  score = 0;
  leftMs = ROUND_MS;
  deadline = Ouse.system.info().uptimeMs + ROUND_MS;
  text("status", "");
  text("level", LEVELS[level].label);
  showScore();
  showTime();
  for (let i = 0; i < HOLES; i++) showHole(i, "hole");
  tickId = Ouse.timers.setInterval(tick, 250);
  schedulePop();
}

function hit(i) {
  if (!running || !isUp(i)) return;
  if (hideIds[i]) {
    Ouse.timers.clear(hideIds[i]);
    delete hideIds[i];
  }
  score++;
  sfx("hit");
  showHole(i, "hammer");
  if (hammerIds[i]) Ouse.timers.clear(hammerIds[i]);
  hammerIds[i] = Ouse.timers.setTimeout(function () {
    delete hammerIds[i];
    showHole(i, "hole");
  }, 160);
  showScore();
}

function persist() {
  Ouse.storage.set("mole", { version: 2, best: best });
}

function openLevel(id) {
  level = id;
  Ouse.navigation.push("game");
}

function bindMenu() {
  Ouse.ui.on("easy", "click", function () { openLevel("easy"); });
  Ouse.ui.on("normal", "click", function () { openLevel("normal"); });
  Ouse.ui.on("hard", "click", function () { openLevel("hard"); });
  showMenuBest();
}

function bindGame() {
  Ouse.ui.on("back", "click", function () {
    running = false;
    clearBoard();
    Ouse.navigation.back();
  });
  Ouse.ui.on("again", "click", function () {
    if (running) return;
    start();
  });
  for (let i = 0; i < HOLES; i++) {
    (function (index) {
      Ouse.ui.on("h" + index, "click", function () { hit(index); });
      Ouse.ui.on("m" + index, "click", function () { hit(index); });
    })(i);
  }
  start();
}

Ouse.navigation.onChange(function (event) {
  page = event.page;
  if (page === "menu") {
    running = false;
    clearTimers();
    bindMenu();
  } else if (page === "game") {
    bindGame();
    resizeBoard();
  }
});

Ouse.storage.get("mole").then(function (value) {
  if (!value) return;
  if (value.version === 2 && value.best) {
    best.easy = Number(value.best.easy) | 0;
    best.normal = Number(value.best.normal) | 0;
    best.hard = Number(value.best.hard) | 0;
  } else if (value.version === 1) {
    best.normal = Number(value.best) | 0;
  }
  if (page === "menu") showMenuBest();
}, function () {});

// BEGIN responsive board
// Embedded in each BPK; layout uses the active page and actual content viewport.
function installResponsiveBoard(config){
 function resize(){
  const page=Ouse.navigation.get().page,c=config[page]||config.main;if(!c)return;
  const d=Ouse.display.get(),w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),land=w>h,a=[];
  function f(id,x,y,width,height){a.push({id,x,y,width:Math.max(1,Math.floor(width)),height:Math.max(1,Math.floor(height))});}
  const before=c.before,after=c.after,top=before.reduce((sum,n)=>sum+n.height+6,8),bottom=after.reduce((sum,n)=>sum+n.height+6,8);
  // Compute each axis separately for rectangular boards such as Klotski.
  const size=Math.max(1,Math.floor(Math.min(((land?w-244:w-16)-2*c.padding-(c.cols-1)*c.gap)/c.cols,((land?h-16:h-top-bottom)-2*c.padding-(c.rows-1)*c.gap)/c.rows)));
  const bw=2*c.padding+c.cols*size+(c.cols-1)*c.gap,bh=2*c.padding+c.rows*size+(c.rows-1)*c.gap,bx=land?8:Math.floor((w-bw)/2),by=land?8:top;
  f(c.board,bx,by,bw,bh);
  let y=8;for(const n of before){f(n.id,land?bw+20:8,y,land?w-bw-28:w-16,n.height);y+=n.height+6;}
  y=land?y+6:by+bh+6;for(const n of after){f(n.id,land?bw+20:8,y,land?w-bw-28:w-16,n.height);y+=n.height+6;}
  for(let r=0;r<c.rows;r++){
   f(c.rowIds[r],c.padding,c.padding+r*(size+c.gap),bw-2*c.padding,size);
   for(let col=0;col<c.cols;col++)f(c.cells[r][col],col*(size+c.gap),0,size,size);
  }
  for(let i=0;i<a.length;i+=64)Ouse.ui.layout(a.slice(i,i+64));
 }
 Ouse.display.onChange(resize);
 const previous=globalThis.app||{},resume=previous.onResume;
 previous.onResume=function(){if(resume)resume();resize();};globalThis.app=previous;
 resize();return resize;
}

const resizeBoard=installResponsiveBoard({"game":{"board":"board","rows":3,"cols":3,"padding":0,"gap":8,"rowIds":["r0","r1","r2"],"cells":[["h0","h1","h2"],["h3","h4","h5"],["h6","h7","h8"]],"before":[{"id":"heading","height":40},{"id":"info","height":28}],"after":[{"id":"status","height":28}]}});
// END responsive board
