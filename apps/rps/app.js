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
