"use strict";
const MODES = ["Ping", "HTTP", "WebSocket", "MQTT"];
const addresses = ["192.168.1.1", "https://www.baidu.com/robots.txt", "", ""];
let mode = 0;
let channel = 0;
let channelMode = 0;
let httpTask = 0;
let generation = 0;
let connected = false;
let logs = [];
let timer = 0;
let paused = false;
let infoTicks = 0;
function text(id, value) { Ouse.ui.setText(id, String(value)); }
function visible(id, value) { if (value) Ouse.ui.show(id); else Ouse.ui.hide(id); }
function brief(value, count) { return String(value == null ? "" : value).slice(0, count || 60); }
function log(value) {
    logs.push(brief(value, 60));
    logs = logs.slice(-4);
    renderLogs();
}
function renderLogs() {
    for (let index = 0; index < 4; index++) {
        text("log" + index, logs[index] || "");
        visible("log" + index, !!logs[index]);
    }
}
function refreshInfo() {
    const wifi = Ouse.wifi.get();
    const state = wifi.connected ? "已连接" : wifi.connecting ? "连接中" : wifi.enabled === false ? "Wi-Fi 已关闭" : "未连接";
    const name = wifi.connected ? wifi.ssid || "Wi-Fi" : state;
    const signal = wifi.connected ? wifi.rssi + " dBm" : "--";
    text("wifiName", name);
    text("wifiAddress", wifi.connected ? wifi.ip + " / " + signal : "IP: --");
    text("infoState", state);
    text("infoName", "Wi-Fi: " + (wifi.connected ? wifi.ssid || "--" : "--"));
    text("infoAddress", "IP: " + (wifi.connected ? wifi.ip || "--" : "--"));
    text("infoSignal", "信号: " + signal);
    const clock = Ouse.clock.get();
    text("infoTime", "时间: " + (clock.syncedThisBoot ? "已同步" : clock.syncing ? "同步中" : clock.failed ? "同步失败" : "未同步"));
}
function status(value) { text("status", brief(value, 70)); }
function api(index) { return index === 0 ? Ouse.net : index === 2 ? Ouse.websocket : Ouse.mqtt; }
function controls() {
    const active = !!channel || !!httpTask;
    text("action", active ? (mode < 2 ? "停止" : "断开") : (mode < 2 ? "开始测试" : "连接"));
    Ouse.ui.setEnabled("send", connected);
    Ouse.ui.setEnabled("subscribe", connected && mode === 3);
    Ouse.ui.setEnabled("address", !active);
}
function stop() {
    ++generation;
    if (httpTask) Ouse.tasks.cancel(httpTask);
    httpTask = 0;
    if (channel) api(channelMode).close(channel);
    channel = 0;
    connected = false;
    controls();
}
function switchMode(index) {
    addresses[mode] = Ouse.ui.getText("address").trim();
    stop();
    mode = index;
    text("title", MODES[mode]);
    text("address", addresses[mode]);
    visible("messageRow", mode >= 2);
    visible("topicRow", mode === 3);
    visible("options", mode === 3);
    visible("wifiCard", mode === 0);
    Ouse.ui.hide("modes");
    logs = [];
    renderLogs();
    refreshInfo();
    status("未开始");
    controls();
}
function bodyText(value) {
    if (typeof value === "string") return brief(value, 60);
    if (!(value instanceof ArrayBuffer)) return "";
    const bytes = new Uint8Array(value);
    let result = "";
    for (let index = 0; index < bytes.length && result.length < 60; index++) {
        const first = bytes[index];
        if (first < 128) { result += first < 32 && first !== 10 ? "." : String.fromCharCode(first); continue; }
        const count = first >= 240 ? 3 : first >= 224 ? 2 : first >= 194 ? 1 : 0;
        let code = first & (count === 3 ? 7 : count === 2 ? 15 : 31);
        let valid = count > 0 && index + count < bytes.length;
        for (let part = 1; valid && part <= count; part++) {
            const next = bytes[index + part];
            if ((next & 192) !== 128) valid = false;
            else code = (code << 6) | (next & 63);
        }
        if (!valid || code > 1114111 || (code >= 55296 && code <= 57343) ||
            code < (count === 1 ? 128 : count === 2 ? 2048 : 65536)) { result += "?"; continue; }
        result += String.fromCodePoint(code);
        index += count;
    }
    return brief(result, 60);
}
function startHttp(url) {
    const stamp = ++generation;
    const started = Ouse.system.info().uptimeMs;
    const request = Ouse.net.request({url, timeoutMs:15000});
    httpTask = request.taskId || 0;
    controls();
    request.then(response => {
        if (stamp !== generation || paused) return;
        httpTask = 0;
        status("HTTP " + response.status + " / " + Math.max(0, Ouse.system.info().uptimeMs - started) + " ms");
        log(response.bodyBytes + " 字节");
        const body = bodyText(response.body);
        if (body) log(body);
        controls();
    }, error => {
        if (stamp !== generation || paused) return;
        httpTask = 0;
        status("请求失败");
        log(error.code === "ESP_ERR_INVALID_SIZE" ? "响应超过 32 KB 限制" : error.message || error.code || "网络错误");
        controls();
    });
}
function start() {
    if (channel || httpTask) { stop(); status("已停止"); return; }
    if (paused) return;
    const address = Ouse.ui.getText("address").trim();
    if (!address) { status("请输入地址"); return; }
    if (!Ouse.wifi.get().connected) { status("未连接 Wi-Fi"); return; }
    if ((mode === 1 && !/^https?:\/\//i.test(address)) ||
        (mode === 2 && !/^wss?:\/\//i.test(address)) ||
        (mode === 3 && !/^mqtts?:\/\//i.test(address))) {
        status("地址协议不正确"); return;
    }
    addresses[mode] = address;
    logs = [];
    renderLogs();
    status(mode < 2 ? "测试中" : "连接中");
    try {
        if (mode === 1) { startHttp(address); return; }
        channelMode = mode;
        const options = {url:address};
        if (mode === 3) {
            options.username = Ouse.ui.getText("username").trim();
            options.password = Ouse.ui.getText("password");
        }
        channel = mode === 0 ? Ouse.net.ping(address) : api(mode).connect(options);
        if (!channel) status("无法创建连接，请稍后重试");
        controls();
    } catch (error) {
        stop();
        status("启动失败");
        log(error.message || error);
    }
}
function poll() {
    if (!paused && ++infoTicks >= 10) { infoTicks = 0; refreshInfo(); }
    if (!channel || paused) return;
    const service = api(channelMode);
    for (let count = 0; count < 4; count++) {
        const event = service.poll(channel);
        if (!event) break;
        if (event.type === "connected") { connected = true; status("已连接"); }
        else if (event.type === "disconnected") { connected = false; status("连接已断开"); }
        else if (event.type === "data") {
            if (event.topic) log(event.topic);
            log("收到: " + bodyText(event.data));
        } else if (event.type === "reply") log("回复: " + event.value + " ms");
        else if (event.type === "timeout") log("请求超时");
        else if (event.type === "complete") status("3 次探测 / " + event.value + " 次回复");
        else if (event.type === "subscribed") log("订阅成功");
        else if (event.type === "published") log("发布确认");
        else if (event.type === "error") { status("连接错误"); log(bodyText(event.data) || event.type); }
    }
    const state = service.get(channel);
    if (!state || state.closed) {
        if (state && state.error && state.error !== "ESP_OK") { status("测试失败"); log(state.error); }
        channel = 0;
        connected = false;
    } else {
        connected = !!state.connected;
        if (state.dropped) status("接收队列已丢弃 " + state.dropped + " 条");
    }
    controls();
}
function send() {
    if (!connected || !channel) return;
    const message = Ouse.ui.getText("message");
    const topic = Ouse.ui.getText("topic").trim();
    if (!message || (mode === 3 && (!topic || /[+#]/.test(topic)))) {
        status(mode === 3 ? "请输入消息和无通配符的主题" : "请输入消息"); return;
    }
    try {
        const accepted = mode === 2 ? Ouse.websocket.send(channel, message) :
            Ouse.mqtt.publish(channel, topic, message, 0, false);
        status(accepted ? "已提交发送" : "发送失败");
        if (accepted) log("发送: " + message);
    } catch (error) { status("发送失败"); log(error.message || error); }
}
Ouse.ui.on("modeBtn", "click", () => Ouse.ui.show("modes"));
for (let index = 0; index < MODES.length; index++)
    Ouse.ui.on("mode" + index, "click", () => switchMode(index));
Ouse.ui.on("action", "click", start);
Ouse.ui.on("send", "click", send);
Ouse.ui.on("options", "click", () => Ouse.ui.show("credentials"));
Ouse.ui.on("credentialsDone", "click", () => Ouse.ui.hide("credentials"));
Ouse.ui.on("networkInfo", "click", () => {
    Ouse.ui.hide("modes");
    refreshInfo();
    Ouse.ui.show("infoSheet");
});
Ouse.ui.on("infoDone", "click", () => Ouse.ui.hide("infoSheet"));
Ouse.ui.on("subscribe", "click", () => {
    if (!connected || !channel) return;
    const topic = Ouse.ui.getText("topic").trim();
    if (!topic) { status("请输入主题"); return; }
    try { status(Ouse.mqtt.subscribe(channel, topic, 0) ? "已提交订阅" : "订阅失败"); }
    catch (error) { status("订阅失败"); log(error.message || error); }
});
function beginPolling() { if (!timer) timer = Ouse.timers.setInterval(poll, 200); }
function pause() {
        paused = true;
        const active = !!channel || !!httpTask;
        stop();
        if (timer) Ouse.timers.clear(timer);
        timer = 0;
        if (active) status("已停止");
}
globalThis.app = {
    onPause: pause,
    onResume() { paused = false; refreshInfo(); beginPolling(); },
    onClose() { pause(); }
};
text("address", addresses[0]);
switchMode(0);
beginPolling();
