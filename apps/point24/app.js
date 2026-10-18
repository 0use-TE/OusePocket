"use strict";
const HANDS = [
  [3, 3, 8, 8], [1, 3, 4, 6], [2, 2, 6, 9], [1, 5, 5, 5], [4, 4, 7, 7],
  [2, 3, 8, 8], [1, 6, 6, 8], [2, 4, 6, 8], [1, 4, 5, 6], [3, 5, 7, 9],
  [2, 5, 5, 10], [1, 2, 7, 7], [4, 5, 6, 7], [1, 1, 8, 8], [2, 3, 4, 12],
  [3, 6, 8, 10], [1, 3, 5, 8], [2, 2, 5, 10], [4, 4, 4, 6], [1, 4, 8, 8]
];

let handIndex = 0;
let cards = [];
let history = [];
let selected = -1;
let op = "";
let cleared = 0;
let best = 0;
let won = false;

function text(id, value) { Ouse.ui.setText(id, String(value)); }
function sfx(id) {
  try { if (Ouse.media.play({ asset: id })) return; } catch (_) {}
  try { Ouse.audio.testTone(); } catch (_) {}
}

function fmt(n) {
  if (!Number.isFinite(n)) return "?";
  const r = Math.round(n * 1000) / 1000;
  if (Math.abs(r - Math.round(r)) < 1e-6) return String(Math.round(r));
  return String(r);
}

function aliveCount() {
  let n = 0;
  for (let i = 0; i < cards.length; i++) if (cards[i] !== null) n++;
  return n;
}

function applyOp(a, operator, b) {
  if (operator === "+") return a + b;
  if (operator === "-") return a - b;
  if (operator === "*") return a * b;
  if (operator === "/") {
    if (Math.abs(b) < 1e-9) return null;
    return a / b;
  }
  return null;
}

function snapshot() {
  return { cards: cards.slice(), selected: selected, op: op, won: won };
}

function paintOps() {
  const map = { "+": "opAdd", "-": "opSub", "*": "opMul", "/": "opDiv" };
  for (const key in map) {
    Ouse.ui.setStyle(map[key], {
      backgroundColor: op === key ? "$accent" : "$button",
      textColor: op === key ? "#ffffff" : "$text"
    });
  }
}

function paint() {
  for (let i = 0; i < 4; i++) {
    const id = "n" + i;
    if (cards[i] === null) {
      text(id, "");
      Ouse.ui.setStyle(id, { backgroundColor: "$border", textColor: "$secondary" });
      Ouse.ui.setEnabled(id, false);
    } else {
      text(id, fmt(cards[i]));
      Ouse.ui.setStyle(id, {
        backgroundColor: selected === i ? "$accent" : "$surface",
        textColor: selected === i ? "#ffffff" : "$text"
      });
      Ouse.ui.setEnabled(id, !won);
    }
  }
  paintOps();
  text("score", "过 " + cleared);
  text("best", "最佳 " + best);
  if (won) text("status", "做成 24!");
  else if (selected >= 0 && op) text("status", "再点一个数");
  else if (selected >= 0) text("status", "点 + - * /");
  else text("status", "点数字再点运算");
}

function loadHand(index) {
  handIndex = ((index % HANDS.length) + HANDS.length) % HANDS.length;
  cards = HANDS[handIndex].slice();
  history = [];
  selected = -1;
  op = "";
  won = false;
  paint();
}

function pushHistory() {
  history.push(snapshot());
  if (history.length > 16) history.shift();
}

function onCard(i) {
  if (won || cards[i] === null) return;
  if (selected < 0) {
    selected = i;
    op = "";
    paint();
    return;
  }
  if (selected === i) {
    selected = -1;
    op = "";
    paint();
    return;
  }
  if (!op) {
    selected = i;
    paint();
    return;
  }
  const a = cards[selected];
  const b = cards[i];
  const result = applyOp(a, op, b);
  if (result === null || !Number.isFinite(result)) {
    text("status", "不能这样算");
    return;
  }
  pushHistory();
  cards[i] = result;
  cards[selected] = null;
  selected = -1;
  op = "";
  sfx("click");
  if (aliveCount() === 1) {
    let value = 0;
    for (let k = 0; k < 4; k++) if (cards[k] !== null) value = cards[k];
    if (Math.abs(value - 24) < 1e-6) {
      won = true;
      cleared++;
      if (cleared > best) best = cleared;
      sfx("win");
      persist();
    } else {
      text("status", "结果是 " + fmt(value));
    }
  }
  paint();
}

function setOp(next) {
  if (won) return;
  if (selected < 0) {
    text("status", "先点一个数");
    return;
  }
  op = next;
  paint();
}

function undo() {
  if (won || !history.length) return;
  const prev = history.pop();
  cards = prev.cards.slice();
  selected = prev.selected;
  op = prev.op;
  won = prev.won;
  paint();
}

function resetHand() {
  loadHand(handIndex);
}

function nextHand() {
  loadHand(handIndex + 1);
}

function persist() {
  Ouse.storage.set("point24", { version: 1, cleared: cleared, best: best, hand: handIndex });
}

Ouse.ui.on("n0", "click", function () { onCard(0); });
Ouse.ui.on("n1", "click", function () { onCard(1); });
Ouse.ui.on("n2", "click", function () { onCard(2); });
Ouse.ui.on("n3", "click", function () { onCard(3); });
Ouse.ui.on("opAdd", "click", function () { setOp("+"); });
Ouse.ui.on("opSub", "click", function () { setOp("-"); });
Ouse.ui.on("opMul", "click", function () { setOp("*"); });
Ouse.ui.on("opDiv", "click", function () { setOp("/"); });
Ouse.ui.on("undo", "click", undo);
Ouse.ui.on("reset", "click", resetHand);
Ouse.ui.on("next", "click", nextHand);

loadHand(0);
Ouse.storage.get("point24").then(function (value) {
  if (value && value.version === 1) {
    cleared = Number(value.cleared) | 0;
    best = Number(value.best) | 0;
    handIndex = Number(value.hand) | 0;
    loadHand(handIndex);
  }
}, function () {});
