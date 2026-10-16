"use strict";
let timer;
function refresh() {
    const info = Ouse.system.info();
    const disk = Ouse.system.storage();
    const diag = Ouse.system.diagnostics();
    const t = key => Ouse.locale.t(key);
    const mib = value => (value / 1048576).toFixed(2) + " MiB";
    Ouse.ui.setText("memory", "RAM  " + mib(info.internalFree) + " / " + mib(info.internalTotal) + "\n" +
        "PSRAM  " + mib(info.psramFree) + " / " + mib(info.psramTotal));
    Ouse.ui.setText("storage", disk.ready ?
        t("used") + mib(disk.usedBytes) + " / " + mib(disk.totalBytes) + "\n" +
        t("free") + mib(disk.freeBytes) + "\n" +
        t("packages") + mib(disk.packageBytes) + " (" + disk.packageCount + ")\n" +
        t("private") + mib(disk.privateBytes) : t("unavailable"));
    Ouse.ui.setText("diagnostics", "Tasks " + diag.nativeTasksActive + "/" + diag.taskActiveLimit + " · BG JS " + diag.backgroundJs + "\n" +
        "JS heap " + mib(diag.jsHeapBytes) + " · registry " + diag.taskRecords + "/" + diag.taskCapacity + "\n" + diag.budgetClock);
    Ouse.ui.setText("system", t("physical") + mib(info.flashBytes) + "\n" +
        t("cpu") + info.cpuMHz + " MHz\n" + t("uptime") + Math.floor(info.uptimeMs / 1000) + " s");
}
function schedule(index) {
    if (timer !== undefined) Ouse.timers.clear(timer);
    timer = Ouse.timers.setInterval(refresh, [1, 2, 5][index] * 1000);
}
Ouse.ui.on("interval", "change", index => { schedule(index); refresh(); });
Ouse.locale.onChange(refresh);
globalThis.app = { onResume() { refresh(); } };
refresh(); schedule(0);
