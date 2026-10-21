"use strict";
const HISTORY = 8;
let timer, tick = 0;
const history = [];
function mib(value) { return (value / 1048576).toFixed(2) + " MiB"; }
function kib(value) { return (value / 1024).toFixed(1) + " KiB"; }
function percent(used, total) {
    if (!total || used < 0) return 0;
    return Math.max(0, Math.min(100, Math.round(used * 100 / total)));
}
function previewLoad() {
    tick += 1;
    return [
        Math.round(32 + 24 * Math.sin(tick / 2.4)),
        Math.round(48 + 20 * Math.sin(tick / 1.7 + 1.1))
    ];
}
function coreLoads(cpu) {
    const measured = cpu.cores.map(core => core && core.valid ? core.busyPercent : null);
    if (measured.some(value => value !== null)) return measured;
    if (cpu.method === "preview-simulated") return previewLoad();
    return measured;
}
function refresh() {
    const info = Ouse.system.info();
    const disk = Ouse.system.storage();
    const cpu = Ouse.system.cpu();
    const t = key => Ouse.locale.t(key);
    const loads = coreLoads(cpu);
    const preview = cpu.method === "preview-simulated" && !cpu.cores.some(core => core && core.valid);
    loads.forEach((load, index) => {
        const known = typeof load === "number" && !Number.isNaN(load);
        Ouse.ui.setValue("cpu" + index, known ? Math.max(0, Math.min(100, Math.round(load))) : 0);
        Ouse.ui.setText("cpu" + index + "Text", (index ? t("core1") : t("core0")) + "  " + (known ? Math.round(load) + "%" : "—"));
    });
    const knownLoads = loads.filter(load => typeof load === "number");
    if (knownLoads.length) {
        const average = knownLoads.reduce((sum, load) => sum + load, 0) / knownLoads.length;
        history.push(Math.max(0, Math.min(100, Math.round(average))));
        if (history.length > HISTORY) history.shift();
    }
    for (let i = 0; i < HISTORY; i++) {
        const sample = history[i - (HISTORY - history.length)];
        Ouse.ui.setValue("h" + i, typeof sample === "number" ? sample : 0);
    }
    Ouse.ui.setText("cpuNote", preview ? t("preview") : knownLoads.length ? t("cpu") + info.cpuMHz + " MHz" : t("waiting"));
    const ramUsed = Math.max(0, info.internalTotal - info.internalFree);
    const psramUsed = Math.max(0, info.psramTotal - info.psramFree);
    Ouse.ui.setValue("ramBar", percent(ramUsed, info.internalTotal));
    Ouse.ui.setText("ramText", t("free") + kib(info.internalFree) + " / " + kib(info.internalTotal));
    Ouse.ui.setValue("psramBar", percent(psramUsed, info.psramTotal));
    Ouse.ui.setText("psramText", t("free") + mib(info.psramFree) + " / " + mib(info.psramTotal));
    Ouse.ui.setValue("storageBar", disk.ready ? percent(disk.usedBytes, disk.totalBytes) : 0);
    Ouse.ui.setText("storage", disk.ready
        ? t("used") + mib(disk.usedBytes) + " / " + mib(disk.totalBytes) + "\n" +
          t("packages") + mib(disk.packageBytes) + " (" + disk.packageCount + ") · " +
          t("private") + mib(disk.privateBytes)
        : t("unavailable"));
}
function schedule(index) {
    if (timer !== undefined) Ouse.timers.clear(timer);
    timer = Ouse.timers.setInterval(refresh, [1, 2, 5][index] * 1000);
}
Ouse.ui.on("interval", "change", index => { schedule(index); refresh(); });
Ouse.locale.onChange(refresh);
globalThis.app = { onResume() { refresh(); } };
refresh();
schedule(0);
