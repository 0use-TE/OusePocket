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
