"use strict";
const SIZE = 8;
const MINE_COUNT = 10;
const CELLS = SIZE * SIZE;
const NUM_COLOR = {
  1: "#1976d2",
  2: "#388e3c",
  3: "#d32f2f",
  4: "#7b1fa2",
  5: "#5d4037",
  6: "#00838f",
  7: "#212121",
  8: "#616161"
};

let mine = [];
let open = [];
let flag = [];
let adj = [];
let placed = false;
let dead = false;
let won = false;
let flagMode = false;
let left = MINE_COUNT;
let startedAt = 0;
let elapsed = 0;
let best = 0;
let tickId = 0;
let alertTask = 0;
let alertStopId = 0;
let appearance = [];
let paintQueue = [];
let paintTimer = 0;
let persistTimer = 0;

function text(id, value) { Ouse.ui.setText(id, String(value)); }

function stopAlert() {
  if (alertStopId) { Ouse.timers.clear(alertStopId); alertStopId = 0; }
  if (alertTask) {
    alertTask = 0;
    try { Ouse.media.stop(); } catch (_) {}
  }
}

function sfx(id) {
  try { if (Ouse.media.play({ asset: id })) return; } catch (_) {}
  try { Ouse.audio.testTone(); } catch (_) {}
}

function playTone() { sfx("click"); }

function playAlert(ms) {
  stopAlert();
  sfx("boom");
  // Keep a short cancelable marker so reopen/reset can cut lingering media.
  alertTask = 1;
  alertStopId = Ouse.timers.setTimeout(function () {
    alertStopId = 0;
    alertTask = 0;
    try { Ouse.media.stop(); } catch (_) {}
  }, ms < 100 ? 100 : ms);
}

function clearTick() {
  if (tickId) { Ouse.timers.clear(tickId); tickId = 0; }
}

function showTime() {
  const sec = dead || won ? elapsed : (startedAt ? Math.floor((Ouse.system.info().uptimeMs - startedAt) / 1000) : 0);
  text("time", sec + "秒");
}

function paintHeader() {
  text("mines", "雷 " + Math.max(0, left));
  text("mode", flagMode ? "旗" : "开");
  showTime();
  if (dead) text("status", "踩雷了");
  else if (won) text("status", best && elapsed === best ? ("通关 " + elapsed + "秒 最佳") : ("通关 " + elapsed + "秒"));
  else if (flagMode) text("status", "插旗");
  else text("status", "");
}

function cellLook(i) {
  let label = "";
  let bg = "$surface";
  let fg = "$text";
  if (open[i]) {
    bg = "$background";
    if (mine[i]) {
      label = "*";
      bg = "$error";
      fg = "#ffffff";
    } else if (adj[i]) {
      label = String(adj[i]);
      fg = NUM_COLOR[adj[i]] || "$text";
    }
  } else if (flag[i]) {
    label = "F";
    fg = "$accent";
  }
  return { label: label, bg: bg, fg: fg, key: label + "|" + bg + "|" + fg };
}

function paintCell(i) {
  const look = cellLook(i);
  if (appearance[i] === look.key) return;
  appearance[i] = look.key;
  const id = "c" + i;
  text(id, look.label);
  Ouse.ui.setStyle(id, { backgroundColor: look.bg, textColor: look.fg });
}

function flushPaint() {
  paintTimer = 0;
  let n = 0;
  while (n < 8 && paintQueue.length) {
    paintCell(paintQueue.shift());
    n++;
  }
  paintHeader();
  if (paintQueue.length) {
    paintTimer = Ouse.timers.setTimeout(flushPaint, 100);
  }
}

function queuePaint(indices) {
  for (let k = 0; k < indices.length; k++) {
    const i = indices[k];
    if (i < 0 || i >= CELLS) continue;
    let found = false;
    for (let q = 0; q < paintQueue.length; q++) if (paintQueue[q] === i) { found = true; break; }
    if (!found) paintQueue.push(i);
  }
  if (!paintTimer && paintQueue.length) flushPaint();
}

function paintAll() {
  appearance = new Array(CELLS);
  paintQueue = [];
  for (let i = 0; i < CELLS; i++) paintQueue.push(i);
  if (paintTimer) { Ouse.timers.clear(paintTimer); paintTimer = 0; }
  flushPaint();
}

function neighbors(i) {
  const r = (i / SIZE) | 0;
  const c = i % SIZE;
  const out = [];
  for (let dr = -1; dr <= 1; dr++) for (let dc = -1; dc <= 1; dc++) {
    if (!dr && !dc) continue;
    const rr = r + dr;
    const cc = c + dc;
    if (rr >= 0 && rr < SIZE && cc >= 0 && cc < SIZE) out.push(rr * SIZE + cc);
  }
  return out;
}

function computeAdj() {
  adj = new Array(CELLS);
  for (let i = 0; i < CELLS; i++) {
    if (mine[i]) { adj[i] = 0; continue; }
    let n = 0;
    const near = neighbors(i);
    for (let k = 0; k < near.length; k++) if (mine[near[k]]) n++;
    adj[i] = n;
  }
}

function placeMines(safe) {
  const banned = {};
  banned[safe] = true;
  const near = neighbors(safe);
  for (let k = 0; k < near.length; k++) banned[near[k]] = true;
  const slots = [];
  for (let i = 0; i < CELLS; i++) if (!banned[i]) slots.push(i);
  mine = new Array(CELLS);
  for (let i = 0; i < CELLS; i++) mine[i] = false;
  for (let n = 0; n < MINE_COUNT && slots.length; n++) {
    const pick = (Math.random() * slots.length) | 0;
    mine[slots[pick]] = true;
    slots.splice(pick, 1);
  }
  computeAdj();
  placed = true;
}

function countOpen() {
  let n = 0;
  for (let i = 0; i < CELLS; i++) if (open[i]) n++;
  return n;
}

function checkWin(changed) {
  if (dead) return;
  if (countOpen() !== CELLS - MINE_COUNT) return;
  won = true;
  elapsed = startedAt ? Math.floor((Ouse.system.info().uptimeMs - startedAt) / 1000) : 0;
  clearTick();
  if (!best || elapsed < best) best = elapsed;
  for (let i = 0; i < CELLS; i++) if (mine[i] && !flag[i]) {
    flag[i] = true;
    changed.push(i);
  }
  left = 0;
  sfx("win");
  queuePaint(changed);
  schedulePersist();
}

function reveal(i) {
  const changed = [];
  if (open[i] || flag[i]) return changed;
  const stack = [i];
  while (stack.length) {
    const cur = stack.pop();
    if (open[cur] || flag[cur]) continue;
    open[cur] = true;
    changed.push(cur);
    if (!mine[cur] && adj[cur] === 0) {
      const near = neighbors(cur);
      for (let k = 0; k < near.length; k++) if (!open[near[k]] && !flag[near[k]]) stack.push(near[k]);
    }
  }
  return changed;
}

function boom(i) {
  dead = true;
  const changed = [i];
  open[i] = true;
  for (let j = 0; j < CELLS; j++) if (mine[j] && !open[j]) {
    open[j] = true;
    changed.push(j);
  }
  elapsed = startedAt ? Math.floor((Ouse.system.info().uptimeMs - startedAt) / 1000) : 0;
  clearTick();
  playAlert(1400);
  queuePaint(changed);
  schedulePersist();
}

function ensureClock() {
  if (startedAt || dead || won) return;
  startedAt = Ouse.system.info().uptimeMs;
  clearTick();
  tickId = Ouse.timers.setInterval(function () {
    if (dead || won) { clearTick(); return; }
    showTime();
  }, 1000);
}

function dig(i) {
  if (dead || won) return;
  if (flag[i] || open[i]) return;
  ensureClock();
  if (!placed) placeMines(i);
  if (mine[i]) { boom(i); return; }
  const changed = reveal(i);
  playTone();
  queuePaint(changed);
  checkWin(changed);
  schedulePersist();
}

function toggleFlag(i) {
  if (dead || won || open[i]) return;
  ensureClock();
  if (!placed) placeMines(i);
  flag[i] = !flag[i];
  left += flag[i] ? -1 : 1;
  playTone();
  queuePaint([i]);
  schedulePersist();
}

function onCell(i) {
  if (flagMode) toggleFlag(i);
  else dig(i);
}

function restart() {
  stopAlert();
  clearTick();
  if (paintTimer) { Ouse.timers.clear(paintTimer); paintTimer = 0; }
  paintQueue = [];
  mine = new Array(CELLS);
  open = new Array(CELLS);
  flag = new Array(CELLS);
  adj = new Array(CELLS);
  for (let i = 0; i < CELLS; i++) {
    mine[i] = false;
    open[i] = false;
    flag[i] = false;
    adj[i] = 0;
  }
  placed = false;
  dead = false;
  won = false;
  left = MINE_COUNT;
  startedAt = 0;
  elapsed = 0;
  paintAll();
  schedulePersist();
}

function schedulePersist() {
  if (persistTimer) return;
  persistTimer = Ouse.timers.setTimeout(function () {
    persistTimer = 0;
    persist();
  }, 100);
}

function persist() {
  Ouse.storage.set("minesweeper", {
    version: 2,
    mine: mine,
    open: open,
    flag: flag,
    adj: adj,
    placed: placed,
    dead: dead,
    won: won,
    flagMode: flagMode,
    left: left,
    startedAt: startedAt,
    elapsed: elapsed,
    best: best,
    uptimeBase: Ouse.system.info().uptimeMs
  });
}

function bind() {
  for (let i = 0; i < CELLS; i++) {
    (function (index) {
      Ouse.ui.on("c" + index, "click", function () { onCell(index); });
    })(i);
  }
  Ouse.ui.on("mode", "click", function () {
    flagMode = !flagMode;
    paintHeader();
    schedulePersist();
  });
  Ouse.ui.on("restart", "click", restart);
}

bind();
paintHeader();
Ouse.storage.get("minesweeper").then(function (value) {
  if (value && (value.version === 1 || value.version === 2) && Array.isArray(value.open) && value.open.length === CELLS) {
    mine = value.mine.map(Boolean);
    open = value.open.map(Boolean);
    flag = value.flag.map(Boolean);
    adj = value.adj.map(function (n) { return Number(n) | 0; });
    placed = !!value.placed;
    dead = !!value.dead;
    won = !!value.won;
    flagMode = !!value.flagMode;
    left = Number(value.left);
    if (!(left === left) || left < 0 || left > MINE_COUNT) left = MINE_COUNT;
    best = Number(value.best) | 0;
    elapsed = Number(value.elapsed) | 0;
    if (!dead && !won && value.startedAt) {
      const base = Number(value.uptimeBase) | 0;
      const prior = Number(value.startedAt) | 0;
      const spent = Math.max(0, base - prior);
      startedAt = Ouse.system.info().uptimeMs - spent;
      clearTick();
      tickId = Ouse.timers.setInterval(function () {
        if (dead || won) { clearTick(); return; }
        showTime();
      }, 1000);
    } else {
      startedAt = 0;
    }
    paintAll();
  } else restart();
}, function () { restart(); });

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

const resizeBoard=installResponsiveBoard({"main":{"board":"board","rows":8,"cols":8,"padding":6,"gap":3,"rowIds":["r0","r1","r2","r3","r4","r5","r6","r7"],"cells":[["c0","c1","c2","c3","c4","c5","c6","c7"],["c8","c9","c10","c11","c12","c13","c14","c15"],["c16","c17","c18","c19","c20","c21","c22","c23"],["c24","c25","c26","c27","c28","c29","c30","c31"],["c32","c33","c34","c35","c36","c37","c38","c39"],["c40","c41","c42","c43","c44","c45","c46","c47"],["c48","c49","c50","c51","c52","c53","c54","c55"],["c56","c57","c58","c59","c60","c61","c62","c63"]],"before":[{"id":"heading","height":40},{"id":"info","height":28}],"after":[{"id":"status","height":24}]}});
// END responsive board
