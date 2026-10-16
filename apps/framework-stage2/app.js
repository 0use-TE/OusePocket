"use strict";
let resumeEpoch = 0;
let result = "ready";
let intent = "none";
let enabled = true;
let targetClicks = 0;
let backgroundTimer;
const OWN_IDS = ["ordinary", "ongoing", "background", "quota0", "quota1", "quota2", "quota3", "quota4", "quota5", "quota6"];

function render() {
    Ouse.ui.setText("ready", "ready=" + resumeEpoch + ";state=" + Ouse.lifecycle.get().state);
    Ouse.ui.setText("result", "result=" + result);
    Ouse.ui.setText("intent", "intent=" + intent);
}
function clean() { for (const id of OWN_IDS) Ouse.notifications.cancel(id); }
function publish() {
    const ordinary = Ouse.notifications.publish({id:"ordinary", title:"Stage2 notice", body:"Initial body", payload:"stage2-initial"});
    const ongoing = Ouse.notifications.publish({id:"ongoing", title:"Stage2 ongoing", body:"Owned background work", ongoing:true, payload:"stage2-ongoing"});
    result = "publish:" + ordinary + ":" + ongoing;
    render();
}
Ouse.ui.on("listAction", "click", () => { result = "list-action"; render(); });
Ouse.ui.on("listSecond", "click", () => { result = "list-second"; render(); });
Ouse.ui.on("changeStyle", "click", () => {
    Ouse.ui.setStyle("card", {backgroundColor:"#244A68", textColor:"#FFFFFF", radius:20});
    Ouse.ui.setStyle("card", {borderWidth:3, borderColor:"#F2C04A"});
    Ouse.ui.setText("cardValue", "Merged style");
    result = "style-merged";
    render();
});
Ouse.ui.on("toggleEnabled", "click", () => {
    enabled = !enabled;
    Ouse.ui.setEnabled("stateTarget", enabled);
    result = "enabled:" + enabled + ";clicks:" + targetClicks;
    render();
});
Ouse.ui.on("stateTarget", "click", () => { result = "target-clicks:" + (++targetClicks); render(); });
Ouse.ui.on("checked", "change", value => { result = "checked:" + value; render(); });
for (const pair of [["openDialog", "dialog"], ["openSheet", "sheet"]]) {
    Ouse.ui.on(pair[0], "click", () => {
        Ouse.ui.show(pair[1]);
        result = "show:" + pair[1] + ":" + Ouse.ui.isVisible(pair[1]);
        render();
    });
    Ouse.ui.on(pair[1], "dismiss", reason => {
        result = "dismiss:" + pair[1] + ":" + reason + ":" + Ouse.ui.isVisible(pair[1]);
        render();
    });
}
for (const pair of [["dialogDone", "dialog"], ["sheetDone", "sheet"]]) {
    Ouse.ui.on(pair[0], "click", () => {
        Ouse.ui.hide(pair[1]);
        result = "hide:" + pair[1] + ":" + Ouse.ui.isVisible(pair[1]);
        render();
    });
}
Ouse.ui.on("publish", "click", publish);
Ouse.ui.on("update", "click", () => {
    result = "update:" + Ouse.notifications.update({id:"ordinary", title:"Stage2 updated", body:"Updated body", payload:"stage2-updated"});
    render();
});
Ouse.ui.on("cancel", "click", () => {
    result = "cancel:" + Ouse.notifications.cancel("ordinary") + ":" + Ouse.notifications.cancel("ongoing");
    render();
});
Ouse.ui.on("background", "click", () => {
    if (backgroundTimer !== undefined) Ouse.timers.clear(backgroundTimer);
    result = "background-armed:" + Ouse.background.begin();
    backgroundTimer = Ouse.timers.setTimeout(() => {
        backgroundTimer = undefined;
        const state = Ouse.lifecycle.get().state;
        result = "background-published:" + Ouse.notifications.publish({id:"background", title:"Stage2 background", body:"Published while " + state, payload:"stage2-background"}) + ":" + state;
        Ouse.background.end();
        render();
    }, 2500);
    render();
});
Ouse.ui.on("quota", "click", () => {
    clean();
    let accepted = "";
    for (let n = 0; n < 7; ++n) accepted += Ouse.notifications.publish({id:"quota" + n, title:"Stage2 quota " + n, payload:"quota"}) ? "1" : "0";
    result = "quota:" + accepted + ";own:" + Ouse.notifications.list().length;
    render();
});
Ouse.ui.on("reset", "click", () => { clean(); result = "reset:" + Ouse.notifications.list().length; render(); });
Ouse.ui.on("listNotices", "click", () => { result = "own:" + Ouse.notifications.list().map(item => item.id).sort().join(","); render(); });
Ouse.notifications.onOpen(event => {
    intent = event.id + ":" + event.payload;
    console.log("stage2-intent=" + intent);
    render();
});
Ouse.lifecycle.onChange(render);
globalThis.app = {onResume() { ++resumeEpoch; render(); }};
render();
