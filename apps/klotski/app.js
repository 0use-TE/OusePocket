"use strict";
const COLS = 4;
const ROWS = 5;
const COLORS = {
  0: "#0F87CC",
  1: "#546E7A",
  2: "#78909C",
  3: "#6D4C41",
  4: "#8D6E63",
  5: "#00897B",
  6: "#A1887F",
  7: "#A1887F",
  8: "#A1887F",
  9: "#A1887F"
};
const NAMES = {
  0: "曹操",
  1: "赵云",
  2: "黄忠",
  3: "马超",
  4: "张飞",
  5: "关羽",
  6: "兵",
  7: "兵",
  8: "兵",
  9: "兵"
};

function cloneStart() {
  return [
    { id: 0, w: 2, h: 2, r: 0, c: 1 },
    { id: 1, w: 1, h: 2, r: 0, c: 0 },
    { id: 2, w: 1, h: 2, r: 0, c: 3 },
    { id: 3, w: 1, h: 2, r: 2, c: 0 },
    { id: 4, w: 1, h: 2, r: 2, c: 3 },
    { id: 5, w: 2, h: 1, r: 2, c: 1 },
    { id: 6, w: 1, h: 1, r: 3, c: 1 },
    { id: 7, w: 1, h: 1, r: 3, c: 2 },
    { id: 8, w: 1, h: 1, r: 4, c: 0 },
    { id: 9, w: 1, h: 1, r: 4, c: 3 }
  ];
}

let pieces = cloneStart();
let selected = -1;
let steps = 0;
let best = 0;
let won = false;

function text(id, value) { Ouse.ui.setText(id, String(value)); }
function sfx(id) {
  try { if (Ouse.media.play({ asset: id })) return; } catch (_) {}
  try { Ouse.audio.testTone(); } catch (_) {}
}

function buildGrid() {
  const grid = [];
  for (let r = 0; r < ROWS; r++) {
    const row = [];
    for (let c = 0; c < COLS; c++) row.push(-1);
    grid.push(row);
  }
  for (let i = 0; i < pieces.length; i++) {
    const p = pieces[i];
    for (let dr = 0; dr < p.h; dr++) {
      for (let dc = 0; dc < p.w; dc++) grid[p.r + dr][p.c + dc] = p.id;
    }
  }
  return grid;
}

function pieceById(id) {
  for (let i = 0; i < pieces.length; i++) if (pieces[i].id === id) return pieces[i];
  return null;
}

function canPlace(p, r, c, grid) {
  if (r < 0 || c < 0 || r + p.h > ROWS || c + p.w > COLS) return false;
  for (let dr = 0; dr < p.h; dr++) {
    for (let dc = 0; dc < p.w; dc++) {
      const cur = grid[r + dr][c + dc];
      if (cur !== -1 && cur !== p.id) return false;
    }
  }
  return true;
}

function tryMove(p, dr, dc) {
  if (!dr && !dc) return false;
  const grid = buildGrid();
  if (!canPlace(p, p.r + dr, p.c + dc, grid)) return false;
  p.r += dr;
  p.c += dc;
  return true;
}

function moveToward(p, tr, tc) {
  const grid = buildGrid();
  const er = p.r;
  const ec = p.c;
  const er2 = p.r + p.h - 1;
  const ec2 = p.c + p.w - 1;
  if (tr >= er && tr <= er2 && tc === ec2 + 1) return tryMove(p, 0, 1);
  if (tr >= er && tr <= er2 && tc === ec - 1) return tryMove(p, 0, -1);
  if (tc >= ec && tc <= ec2 && tr === er2 + 1) return tryMove(p, 1, 0);
  if (tc >= ec && tc <= ec2 && tr === er - 1) return tryMove(p, -1, 0);
  if (tr >= er && tr <= er2) {
    if (tc > ec2 && canPlace(p, p.r, p.c + 1, grid)) return tryMove(p, 0, 1);
    if (tc < ec && canPlace(p, p.r, p.c - 1, grid)) return tryMove(p, 0, -1);
  }
  if (tc >= ec && tc <= ec2) {
    if (tr > er2 && canPlace(p, p.r + 1, p.c, grid)) return tryMove(p, 1, 0);
    if (tr < er && canPlace(p, p.r - 1, p.c, grid)) return tryMove(p, -1, 0);
  }
  return false;
}

function checkWin() {
  const cao = pieceById(0);
  return !!(cao && cao.r === 3 && cao.c === 1);
}

function labelCell(p, r, c) {
  // Put the name once, near the visual center of the merged block.
  const lr = p.r + ((p.h - 1) / 2 | 0);
  const lc = p.c + ((p.w - 1) / 2 | 0);
  return r === lr && c === lc;
}

function paint() {
  const grid = buildGrid();
  for (let r = 0; r < ROWS; r++) {
    for (let c = 0; c < COLS; c++) {
      const id = "c" + (r * COLS + c);
      const pid = grid[r][c];
      if (pid < 0) {
        text(id, "");
        Ouse.ui.setStyle(id, {
          backgroundColor: selected >= 0 ? "#607D8B" : "#455A64",
          textColor: "#ffffff",
          radius: 0,
          borderWidth: 0
        });
      } else {
        const p = pieceById(pid);
        text(id, p && labelCell(p, r, c) ? (NAMES[pid] || "") : "");
        Ouse.ui.setStyle(id, {
          backgroundColor: pid === selected ? "$accent" : (COLORS[pid] || "$surface"),
          textColor: "#ffffff",
          radius: 0,
          borderWidth: 0
        });
      }
    }
  }
  text("steps", "步 " + steps);
  text("best", best ? ("最佳 " + best) : "最佳 --");
  if (won) text("status", "曹操脱险!");
  else if (selected >= 0) text("status", "再点空位移动");
  else text("status", "点块再点空位移动");
}

function onCell(index) {
  if (won) return;
  const r = (index / COLS) | 0;
  const c = index % COLS;
  const grid = buildGrid();
  const pid = grid[r][c];
  if (selected < 0) {
    if (pid < 0) return;
    selected = pid;
    paint();
    return;
  }
  if (pid === selected) {
    selected = -1;
    paint();
    return;
  }
  if (pid >= 0) {
    selected = pid;
    paint();
    return;
  }
  const p = pieceById(selected);
  if (!p) { selected = -1; paint(); return; }
  if (!moveToward(p, r, c)) {
    text("status", "不能这样走");
    return;
  }
  steps++;
  selected = -1;
  sfx("click");
  if (checkWin()) {
    won = true;
    if (!best || steps < best) best = steps;
    sfx("win");
    persist();
  }
  paint();
}

function restart() {
  pieces = cloneStart();
  selected = -1;
  steps = 0;
  won = false;
  paint();
}

function persist() {
  Ouse.storage.set("klotski", { version: 1, best: best });
}

for (let i = 0; i < ROWS * COLS; i++) {
  (function (index) {
    Ouse.ui.on("c" + index, "click", function () { onCell(index); });
  })(i);
}
Ouse.ui.on("restart", "click", restart);
paint();
Ouse.storage.get("klotski").then(function (value) {
  if (value && value.version === 1) {
    best = Number(value.best) | 0;
    paint();
  }
}, function () {});
