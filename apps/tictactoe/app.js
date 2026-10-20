"use strict";
const WINS = [
  [0, 1, 2], [3, 4, 5], [6, 7, 8],
  [0, 3, 6], [1, 4, 7], [2, 5, 8],
  [0, 4, 8], [2, 4, 6]
];
let board = [0, 0, 0, 0, 0, 0, 0, 0, 0];
let turn = 1;
let over = false;
let win = 0;
let lose = 0;
let draw = 0;
let busy = false;

function text(id, value) { Ouse.ui.setText(id, String(value)); }
function sfx(id) {
  try { if (Ouse.media.play({ asset: id })) return; } catch (_) {}
  try { Ouse.audio.testTone(); } catch (_) {}
}

function winner() {
  for (let i = 0; i < WINS.length; i++) {
    const a = WINS[i][0], b = WINS[i][1], c = WINS[i][2];
    if (board[a] && board[a] === board[b] && board[a] === board[c]) return board[a];
  }
  return 0;
}

function full() {
  for (let i = 0; i < 9; i++) if (!board[i]) return false;
  return true;
}

function paint() {
  for (let i = 0; i < 9; i++) {
    const v = board[i];
    text("c" + i, v === 1 ? "X" : v === 2 ? "O" : "");
    Ouse.ui.setStyle("c" + i, {
      textColor: v === 1 ? "$accent" : v === 2 ? "$error" : "$text",
      backgroundColor: "$surface"
    });
  }
  text("score", "胜 " + win + " 负 " + lose + " 平 " + draw);
  if (over) {
    const w = winner();
    if (w === 1) text("status", "你赢了");
    else if (w === 2) text("status", "你输了");
    else text("status", "平局");
  } else if (busy) text("status", "电脑思考");
  else text("status", "你的回合");
}

function endRound() {
  over = true;
  const w = winner();
  if (w === 1) win++;
  else if (w === 2) lose++;
  else draw++;
  sfx(w === 1 ? "win" : "click");
  paint();
  persist();
}

function aiMove() {
  if (over) return;
  let choice = -1;
  for (let mark = 2; mark >= 1 && choice < 0; mark--) {
    for (let i = 0; i < 9; i++) {
      if (board[i]) continue;
      board[i] = mark;
      const w = winner();
      board[i] = 0;
      if (w === mark) { choice = i; break; }
    }
  }
  if (choice < 0 && !board[4]) choice = 4;
  if (choice < 0) {
    const corners = [0, 2, 6, 8].filter(function (i) { return !board[i]; });
    if (corners.length) choice = corners[(Math.random() * corners.length) | 0];
  }
  if (choice < 0) {
    const open = [];
    for (let i = 0; i < 9; i++) if (!board[i]) open.push(i);
    if (open.length) choice = open[(Math.random() * open.length) | 0];
  }
  busy = false;
  if (choice >= 0) {
    board[choice] = 2;
    turn = 1;
    if (winner() || full()) endRound();
    else paint();
  } else paint();
}

function play(i) {
  if (over || busy || board[i] || turn !== 1) return;
  board[i] = 1;
  sfx("click");
  if (winner() || full()) { endRound(); return; }
  turn = 2;
  busy = true;
  paint();
  Ouse.timers.setTimeout(aiMove, 250);
}

function restart() {
  board = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  turn = 1;
  over = false;
  busy = false;
  paint();
  persist();
}

function persist() {
  Ouse.storage.set("tictactoe", { version: 1, win: win, lose: lose, draw: draw });
}

for (let i = 0; i < 9; i++) {
  (function (index) {
    Ouse.ui.on("c" + index, "click", function () { play(index); });
  })(i);
}
Ouse.ui.on("restart", "click", restart);
paint();
Ouse.storage.get("tictactoe").then(function (value) {
  if (value && value.version === 1) {
    win = Number(value.win) | 0;
    lose = Number(value.lose) | 0;
    draw = Number(value.draw) | 0;
    paint();
  }
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

const resizeBoard=installResponsiveBoard({"main":{"board":"board","rows":3,"cols":3,"padding":8,"gap":8,"rowIds":["r0","r1","r2"],"cells":[["c0","c1","c2"],["c3","c4","c5"],["c6","c7","c8"]],"before":[{"id":"heading","height":40},{"id":"status","height":28}],"after":[{"id":"score","height":28}]}});
// END responsive board
