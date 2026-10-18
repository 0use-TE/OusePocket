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
