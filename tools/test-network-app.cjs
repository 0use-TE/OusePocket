const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

function load() {
    const texts = {}, enabled = {}, handlers = {}, shown = new Set(), calls = [];
    const states = new Map(), events = [], timers = new Map();
    let identifier = 0, now = 1000, online = true, resolveHttp, rejectHttp;
    const open = (kind, options) => {
        calls.push([kind, options]);
        states.set(++identifier, {connected:false, closed:false, error:"ESP_OK", dropped:0});
        return identifier;
    };
    const channel = kind => ({
        connect: options => open(kind, options),
        get: id => states.get(id), poll: () => events.shift() || null,
        close(id) { calls.push(["close", id]); states.get(id).closed = true; return true; },
        send: (...args) => { calls.push(["send", ...args]); return true; },
        subscribe: (...args) => { calls.push(["subscribe", ...args]); return true; },
        publish: (...args) => { calls.push(["publish", ...args]); return true; }
    });
    const net = channel("net");
    net.ping = host => open("ping", host);
    net.request = options => {
        calls.push(["http", options]);
        const promise = new Promise((resolve, reject) => { resolveHttp = resolve; rejectHttp = reject; });
        promise.taskId = ++identifier;
        return promise;
    };
    const Ouse = {
        ui:{on:(id, event, fn) => { handlers[id] = fn; }, setText:(id, value) => { texts[id] = value; },
            getText:id => texts[id] || "", setEnabled:(id, value) => { enabled[id] = value; },
            show:id => shown.add(id), hide:id => shown.delete(id)},
        net, websocket:channel("ws"), mqtt:channel("mqtt"),
        wifi:{get:() => ({connected:online, enabled:true, ssid:"Test Wi-Fi", ip:"192.168.1.9", rssi:-55})},
        clock:{get:() => ({syncedThisBoot:true})}, system:{info:() => ({uptimeMs:now})},
        tasks:{cancel:id => calls.push(["cancel", id])},
        timers:{setInterval:fn => { timers.set(++identifier, fn); return identifier; }, clear:id => timers.delete(id)}
    };
    const context = vm.createContext({Ouse, ArrayBuffer, Uint8Array});
    vm.runInContext(fs.readFileSync("apps/network/app.js", "utf8"), context);
    return {texts, enabled, shown, calls, states, events, lifecycle:context.app,
        click:id => handlers[id](), tick:() => [...timers.values()].forEach(fn => fn()),
        online:value => { online = value; }, now:value => { now = value; },
        connect() { const id = [...states.keys()].at(-1); states.get(id).connected = true; events.push({type:"connected"}); this.tick(); return id; },
        resolve:value => resolveHttp(value), reject:value => rejectHttp(value), timerCount:() => timers.size};
}

(async () => {
    const app = load();
    assert.equal(app.texts.wifiName, "Test Wi-Fi"); assert(app.shown.has("wifiCard"));
    app.click("modeBtn"); app.click("networkInfo"); assert(app.shown.has("infoSheet"));
    assert(!app.shown.has("modes")); assert.equal(app.texts.infoAddress, "IP: 192.168.1.9");
    assert.equal(app.texts.infoSignal, "信号: -55 dBm"); assert.equal(app.texts.infoTime, "时间: 已同步");
    app.click("infoDone"); assert(!app.shown.has("infoSheet"));
    app.online(false); app.click("action"); assert.equal(app.texts.status, "未连接 Wi-Fi");
    for (let tick = 0; tick < 10; tick++) app.tick();
    assert.equal(app.texts.wifiName, "未连接"); assert.equal(app.texts.wifiAddress, "IP: --");
    assert.equal(app.calls.length, 0);
    app.online(true); app.click("action"); app.click("action");
    assert.equal(app.calls[0][0], "ping"); assert.equal(app.calls[1][0], "close");
    app.click("action");
    for (let index = 0; index < 3; index++) app.events.push({type:"reply", value:7});
    app.events.push({type:"complete", value:3});
    [...app.states.values()].at(-1).closed = true;
    app.tick(); assert.equal(app.texts.status, "3 次探测 / 3 次回复");
    assert.equal(app.texts.action, "开始测试");
    app.click("modeBtn"); assert(app.shown.has("modes")); app.click("mode1");
    assert(!app.shown.has("wifiCard"));
    app.click("action"); assert.equal(app.calls.at(-1)[0], "http");
    app.now(1035); app.resolve({status:200, bodyBytes:500, body:"x".repeat(500)});
    await Promise.resolve(); assert.equal(app.texts.status, "HTTP 200 / 35 ms");
    assert(app.texts.log1.length <= 60);
    app.click("action"); app.click("mode2");
    const status = app.texts.status;
    app.resolve({status:200, bodyBytes:1, body:"stale"}); await Promise.resolve();
    assert.equal(app.texts.status, status); assert(app.calls.some(call => call[0] === "cancel"));
    app.texts.address = "http://wrong"; app.click("action"); assert.equal(app.texts.status, "地址协议不正确");
    app.texts.address = "ws://192.168.1.2:8765"; app.click("action");
    const ws = app.connect(); assert(app.enabled.send);
    app.texts.message = "hello"; app.click("send"); assert.deepEqual(app.calls.at(-1), ["send", ws, "hello"]);
    app.events.push({type:"data", data:"echo"}); app.tick(); assert(Object.values(app.texts).includes("收到: echo"));
    app.click("mode3"); assert(app.states.get(ws).closed);
    app.texts.address = "mqtt://192.168.1.2:1883";
    app.texts.username = "test"; app.texts.password = "secret"; app.click("action");
    assert.equal(app.calls.at(-1)[1].password, "secret");
    const mqtt = app.connect(); app.texts.topic = "test/#"; app.click("subscribe");
    assert.deepEqual(app.calls.at(-1), ["subscribe", mqtt, "test/#", 0]);
    app.texts.message = "test"; const before = app.calls.length; app.click("send"); assert.equal(app.calls.length, before);
    app.texts.topic = "test/topic"; app.click("send");
    assert.deepEqual(app.calls.at(-1), ["publish", mqtt, "test/topic", "test", 0, false]);
    const payload = new TextEncoder().encode("中文").buffer;
    app.events.push({type:"data", topic:"test/topic", data:payload}); app.tick();
    assert(Object.values(app.texts).includes("收到: 中文"));
    app.events.push({type:"data", data:new Uint8Array([255, 192, 128]).buffer}); app.tick();
    assert(![app.texts.status, ...[0, 1, 2, 3].map(index => app.texts["log" + index])].some(value => value.includes("secret")));
    app.lifecycle.onPause(); assert.equal(app.timerCount(), 0); assert(app.states.get(mqtt).closed);
    app.lifecycle.onResume(); app.lifecycle.onResume(); assert.equal(app.timerCount(), 1);
    app.click("mode1"); app.click("action"); app.reject({message:"TLS failed"}); await Promise.resolve();
    assert.equal(app.texts.status, "请求失败");
    app.lifecycle.onClose.call(undefined); assert.equal(app.timerCount(), 0);
    console.log("PASS network app: mode sheet, offline/URL guards, ping, HTTP success/error/cancel/stale completion, WS send/receive, MQTT auth/subscribe/publish/UTF-8, lifecycle cleanup");
})().catch(error => { console.error(error); process.exitCode = 1; });
