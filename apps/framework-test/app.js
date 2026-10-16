"use strict";
let ticks = 0;
let result = "ready";
let inputStatus = "none";
let pausedAt = 0;
let resumeGap = 0;
let resumeEpoch = 0;
let resumeProbe = false;
let beginOnSuspend = false;
let deferredEnd;
let deferredHome;

function render() {
    const state = Ouse.lifecycle.get();
    Ouse.ui.setText("ticks", "ticks=" + ticks);
    Ouse.ui.setText("lifecycle", "state=" + state.state + ";background=" + Ouse.background.isActive());
    Ouse.ui.setText("result", "result=" + result);
    Ouse.ui.setText("cycle", "resumeEpoch=" + resumeEpoch + ";resumeGap=" + resumeGap);
    Ouse.ui.setText("inputStatus", "input=" + inputStatus);
}

Ouse.timers.setInterval(() => {
    ++ticks;
    if (Ouse.lifecycle.get().visible) render();
}, 100);

Ouse.ui.on("begin", "click", () => { result = "begin:" + Ouse.background.begin(); render(); });
Ouse.ui.on("end", "click", () => { result = "end:" + Ouse.background.end(); render(); });
Ouse.ui.on("delayedEnd", "click", () => {
    if (deferredEnd !== undefined) Ouse.timers.clear(deferredEnd);
    Ouse.background.begin();
    result = "end-armed";
    deferredEnd = Ouse.timers.setTimeout(() => {
        deferredEnd = undefined;
        result = "ended-in-background";
        Ouse.background.end();
    }, 1000);
    render();
});
Ouse.ui.on("delayedHome", "click", () => {
    if (deferredHome !== undefined) Ouse.timers.clear(deferredHome);
    Ouse.background.begin();
    result = "home-armed";
    deferredHome = Ouse.timers.setTimeout(() => {
        deferredHome = undefined;
        result = "background-home:" + Ouse.home();
    }, 1000);
    render();
});
Ouse.ui.on("switch", "change", value => { result = "switch:" + value; render(); });
Ouse.ui.on("checkbox", "change", value => { result = "checkbox:" + value; render(); });
Ouse.ui.on("dropdown", "change", value => {
    Ouse.ui.setValue("progress", (value + 1) * 25);
    result = "dropdown:" + value;
    render();
});
for (const id of ["resizeInput", "numberInput", "secretInput", "overlayInput"]) {
    Ouse.ui.on(id, "change", value => {
        inputStatus = id === "secretInput" ? id + ":length=" + value.length : id + ":" + value;
        render();
    });
    Ouse.ui.on(id, "submit", () => {
        const value = Ouse.ui.getText(id);
        inputStatus = id === "secretInput" ? id + ":submitted-length=" + value.length : id + ":submitted=" + value;
        render();
    });
}
Ouse.ui.on("home", "click", () => Ouse.home());
Ouse.lifecycle.onChange(state => {
    if (beginOnSuspend && state.state === "suspended") {
        result = "begin-on-suspend:" + Ouse.background.begin();
    }
    render();
});
Ouse.display.onChange(() => {
    const d = Ouse.display.get();
    console.log("viewport=" + d.contentWidthDp + "x" + d.contentHeightDp +
        ";keyboard=" + d.keyboardVisible + ";inset=" + d.keyboardInsetDp);
});
globalThis.app = {
    onPause() { pausedAt = ticks; },
    onResume() {
        ++resumeEpoch;
        resumeGap = ticks - pausedAt;
        if (resumeProbe) {
            const started = Date.now();
            Ouse.timers.setTimeout(() => {
                result = "resume-timer:" + (Date.now() - started);
                console.log(result);
                render();
            }, 100);
        }
        render();
    }
};
render();
