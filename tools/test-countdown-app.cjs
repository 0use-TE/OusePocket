const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
function load() {
    const handlers = {}, texts = {minutes:"", seconds:"2"}, timers = new Map(), notifications = [], shown = new Set(), cancelled = [], enabled = {};
    let now = 100, background = false, tones = 0, identifier = 0;
    let toneState = "succeeded", openNotification, rejectTone = false;
    const Ouse = {
        ui:{on:(id, event, fn) => { handlers[id] = fn; }, setText:(id, value) => { texts[id] = value; },
            getText:id => texts[id], getValue:() => 0, setEnabled:(id, value) => { enabled[id] = value; },
            show:id => shown.add(id), hide:id => shown.delete(id)},
        background:{isActive:() => background, begin:() => { background = true; }, end:() => { background = false; }},
        timers:{setInterval:fn => { timers.set(++identifier, fn); return identifier; }, clear:id => timers.delete(id)},
        system:{info:() => ({uptimeMs:now})}, audio:{alert:() => { ++tones; return rejectTone ? false : 700; }},
        tasks:{get:() => ({state:toneState}), cancel:id => cancelled.push(id)},
        notifications:{publish:options => notifications.push(options), cancel() {}, onOpen:fn => { openNotification = fn; }}
    };
    const context = vm.createContext({Ouse});
    vm.runInContext(fs.readFileSync("apps/countdown/app.js", "utf8"), context);
    return {texts, notifications, shown, cancelled, enabled, lifecycle:context.app, click:id => handlers[id](),
        open:() => openNotification(), toneState:value => { toneState = value; }, rejectTone:value => { rejectTone = value; },
        advance:milliseconds => { now += milliseconds; [...timers.values()].forEach(fn => fn()); },
        tones:() => tones, background:() => background, timers:() => timers.size};
}
{
    const app = load(); app.click("start"); assert(app.background());
    app.advance(2500); assert.equal(app.tones(), 1); assert.equal(app.notifications.length, 1);
    assert.equal(app.notifications[0].title, "时间到"); assert.equal(app.texts.remain, "00:00");
    app.advance(1000); assert.equal(app.tones(), 2); assert(app.background()); assert(app.shown.has("doneDialog"));
    assert(!app.enabled.start); app.click("start"); app.click("cancel");
    assert.equal(app.timers(), 1); app.advance(1000); assert.equal(app.tones(), 3);
    app.shown.clear(); app.lifecycle.onResume(); assert(app.shown.has("doneDialog"));
    app.shown.clear(); app.open(); assert(app.shown.has("doneDialog"));
    app.click("confirmDone"); assert(!app.background()); assert(!app.shown.has("doneDialog"));
    assert(app.cancelled.includes(700)); app.advance(9000); assert.equal(app.tones(), 3);
    assert.equal(app.notifications.length, 1); assert.equal(app.timers(), 0);
}
{
    const app = load(); app.click("start"); app.advance(500); app.click("pause");
    app.advance(9000); assert.equal(app.tones(), 0); app.click("resume");
    app.advance(1600); assert.equal(app.tones(), 1);
    app.lifecycle.onClose(); assert.equal(app.timers(), 0); assert(!app.background());
}
{
    const app = load(); app.click("start"); app.click("cancel"); app.advance(9000);
    assert.equal(app.tones(), 0); assert.equal(app.notifications.length, 0);
    app.click("start"); app.lifecycle.onClose(); assert.equal(app.timers(), 0); assert(!app.background());
}
{
    const app = load(); app.click("start"); app.advance(2500); app.toneState("running");
    app.advance(1000); assert.equal(app.tones(), 1);
    app.toneState("succeeded"); app.advance(1000); assert.equal(app.tones(), 2);
    app.click("confirmDone");
}
{
    const app = load(); app.rejectTone(true); app.click("start"); app.advance(2500);
    app.advance(1000); assert.equal(app.tones(), 2); assert.equal(app.notifications.length, 1);
    app.rejectTone(false); app.advance(1000); assert.equal(app.tones(), 3); app.click("confirmDone");
    app.advance(1000); assert.equal(app.tones(), 3);
}
console.log("PASS countdown: monotonic timer, repeat until confirmation, one notification, background lease, modal reopen, no overlapping tones, audio retry, pause/resume/cancel/close");
