// Read the shipped BPK resources and execute its real entry script. Only service I/O is mocked.
// This does not compile firmware, open a serial port, or install an application.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const source = path.resolve(__dirname, '../examples/resource-monitor/src');
const read = name => JSON.parse(fs.readFileSync(path.join(source, name), 'utf8'));
const manifest = read('manifest.json');
const screen = read('res/screens/main.json');
const entry = path.resolve(source, manifest.runtime.entry);
const code = fs.readFileSync(entry, 'utf8');
const nodes = new Map(), declaredActions = new Set();
function index(node, parent = '') {
  const current = parent + '/' + node.id;
  assert.ok(!nodes.has(current), `Duplicate GUI path: ${current}`);
  nodes.set(current, node);
  for (const event of node.events || []) declaredActions.add(event.action);
  for (const child of node.children || []) index(child, current);
}
index(screen);
const pages = ['overview', 'memory', 'storage', 'system'];
const actions = [...pages, 'hold', 'refresh'].map(value => 'resources.' + value);
for (const action of actions) assert.ok(declaredActions.has(action), `Missing action ${action}`);
assert.equal(manifest.runtime.type, 'JavaScript');
assert.equal(manifest.package.version, '1.0.1');
assert.ok(entry.startsWith(source + path.sep), 'Runtime entry escaped the package');
assert.ok(manifest.services.some(service => service.name === 'Pocket' && service.version === '1.0.0'));

function checkResources() {
  const root = read('res/root.json');
  for (const asset of [...root.assets, ...(root.variants || []).flatMap(variant => variant.assets || [])]) {
    const resolved = path.resolve(source, 'res', asset);
    assert.ok(resolved.startsWith(source + path.sep + 'res' + path.sep), `External asset ${asset}`);
    assert.ok(fs.existsSync(resolved), `Missing asset ${asset}`);
    JSON.parse(fs.readFileSync(resolved, 'utf8'));
  }
  const profile = read('res/profile.json');
  assert.ok(fs.existsSync(path.resolve(source, 'res', profile.root)));
  const images = read('res/images/index.json').images;
  assert.ok(images.some(image => image.id === profile.icon_id), 'Launcher icon is not declared');
  for (const image of images) {
    const file = path.resolve(source, 'res/images', image.src);
    assert.ok(file.startsWith(source + path.sep + 'res' + path.sep), `External image ${image.src}`);
    const png = fs.readFileSync(file);
    assert.equal(png.subarray(0, 8).toString('hex'), '89504e470d0a1a0a', `Invalid PNG ${image.src}`);
    assert.equal(png.readUInt32BE(16), image.width, `Image width ${image.src}`);
    assert.equal(png.readUInt32BE(20), image.height, `Image height ${image.src}`);
  }
  const english = read('res/constants/en.json').data.ui;
  const chinese = read('res/constants/zh.json').data.ui;
  assert.deepEqual(Object.keys(chinese).sort(), Object.keys(english).sort(), 'Locale key mismatch');
  const resources = JSON.stringify(screen);
  for (const [, key] of resources.matchAll(/\$\{constant\.ui\.([A-Za-z0-9_]+)\}/g)) {
    assert.equal(typeof english[key], 'string', `Missing English string ${key}`);
    assert.equal(typeof chinese[key], 'string', `Missing Chinese string ${key}`);
  }
  return {en: english, zh: chinese};
}
const locales = checkResources();

const flush = () => new Promise(resolve => setImmediate(resolve));
function fixture(locale = 'en') {
  const io = [], labels = new Map(), values = new Map(), hidden = new Map(), waiting = [];
  const timers = new Set(), started = new Set(), plans = new Map(), counts = new Map();
  let clock = 100000, timerId = 0, active = 0, maximumActive = 0, snapshotNumber = 0;
  let load = null;
  for (const [current, node] of nodes) hidden.set(current, Boolean(node.commonProps?.hidden));
  const response = data => JSON.stringify({success: true, data});
  function sample() {
    const currentLoad = load === undefined ? null : load === null ? (snapshotNumber++ === 0 ? null : 40) : load;
    const memory = {total: 400000, free: 240000, used: 160000, minimumFree: 200000, largestFree: 180000, largestAgeMs: 0};
    return {ok: true, schemaVersion: 1, capturedAtMs: clock, uptimeMs: clock,
      cpu: {frequencyMHz: 200, cores: [{id: 0, loadPercent: currentLoad}, {id: 1, loadPercent: currentLoad}],
        loadPercent: currentLoad, valid: currentLoad !== null, intervalMs: currentLoad === null ? 0 : 1000},
      memory: {internal: memory, psram: {...memory, total: 28000000, free: 20000000, used: 8000000, minimumFree: 18000000, largestFree: 17000000}},
      system: {chipModel: 'ESP32-P4', chipRevision: '3.2', cores: 2, resetReason: 'power_on', resetReasonCode: 1,
        firmwareVersion: 'resource-test', idfVersion: 'v6.1'}, sampleDurationUs: 80};
  }
  function storage() {
    return {ok: true, schemaVersion: 1, capturedAtMs: clock,
      flash: {totalBytes: 16777216, partitions: [
        {label: 'factory', type: 'app', offset: 65536, size: 8388608},
        {label: 'littlefs', type: 'data', offset: 8454144, size: 8323072}]},
      firmware: {projectName: 'ouse_brookesia_minimal', version: 'resource-test', idfVersion: 'v6.1',
        partition: 'factory', partitionBytes: 8388608, imageBytes: 7855088},
      internal: {available: true, total: 8323072, used: 2097152, free: 6225920},
      sd: {mounted: false, available: false, total: 0, used: 0, free: 0}};
  }
  function sync(handle, name, params) {
    const p = JSON.parse(params); io.push({handle, name, p});
    assert.ok(started.has(handle), `Call after service release: ${handle}.${name}`);
    if (handle === 'SystemGui') {
      if (name === 'GetConstant') return response({Value: locales[locale]});
      if (name === 'SubscribeAction') assert.ok(declaredActions.has(p.Action) || p.Action === 'system.navigation.back', p.Action);
      else if (name === 'SetText') {
        assert.equal(nodes.get(p.Path)?.type, 'label', `Missing label ${p.Path}`);
        assert.equal(typeof p.Text, 'string');
        assert.doesNotMatch(p.Text, /NaN|undefined|\[object Object\]/, `Invalid label ${p.Path}`);
        labels.set(p.Path, p.Text);
      } else if (name === 'SetValue') {
        const node = nodes.get(p.Path);
        assert.equal(node?.type, 'progressBar', `Missing progress bar ${p.Path}`);
        assert.ok(Number.isFinite(p.Value) && p.Value >= node.rangeProps.min && p.Value <= node.rangeProps.max, p.Path);
        values.set(p.Path, p.Value);
      } else if (name === 'SetBinding') {
        const node = nodes.get(p.Path);
        assert.ok(node && Object.values(node.bindings || {}).includes(p.Key), `Undeclared binding store key ${p.Path}: ${p.Key}`);
        if (p.Key === 'commonProps.hidden' || p.Key === 'hidden') hidden.set(p.Path, p.Value === true || p.Value === 'true');
      } else if (!['GetConstant', 'SubscribeAction'].includes(name)) assert.fail(`Unexpected GUI method ${name}`);
    } else if (handle === 'SystemTimer') {
      if (name === 'StartPeriodic') {
        assert.equal(p.IntervalMs, 1000, 'Resource refresh must use the planned one-second interval');
        assert.equal(timers.size, 0, 'Duplicate active refresh timer');
        timers.add(++timerId); return response(timerId);
      }
      assert.equal(name, 'Stop'); assert.ok(timers.delete(p.TimerId), 'Stopped an unknown timer');
    } else assert.fail(`Unexpected synchronous call ${handle}.${name}`);
    return response(null);
  }
  const bridge = {
    start_service(name) {
      if (!['SystemGui', 'SystemCore', 'SystemTimer'].includes(name)) {
        assert.ok(manifest.services.some(service => service.name === name), `Undeclared service ${name}`);
      }
      assert.ok(!started.has(name), `Service started twice ${name}`); started.add(name); io.push({name: 'start', handle: name}); return name;
    },
    stop_service(handle) {assert.ok(started.delete(handle), `Service released twice ${handle}`); io.push({handle, name: 'release'});},
    call_function: sync,
    call_function_async(handle, name, params, timeoutMs) {
      assert.ok(started.has(handle), `Async call after service release ${handle}`);
      assert.equal(handle, 'Pocket'); assert.equal(name, 'Request');
      const p = JSON.parse(params); const op = p.Request.Op;
      assert.ok(['ResourceSnapshot', 'ResourceStorage'].includes(op), `Unexpected Pocket operation ${op}`);
      assert.equal(timeoutMs, 5000, 'Every metrics request needs an explicit native timeout');
      io.push({handle, name, p, timeoutMs}); counts.set(op, (counts.get(op) || 0) + 1);
      ++active; maximumActive = Math.max(maximumActive, active);
      const plan = plans.get(op)?.shift();
      let raw = plan?.response;
      if (raw === undefined) {
        const data = op === 'ResourceSnapshot' ? sample() : storage();
        raw = response(plan?.transform ? plan.transform(data) : data);
      }
      // QuickJS rejects native async failures with a JS string, while local JS
      // validation throws Error objects. Exercise both contracts.
      const rejection = plan?.rawReject ? plan.reject : new Error(plan?.reject);
      const pending = plan?.defer ? new Promise((resolve, reject) => waiting.push({op, finish() {plan.reject ? reject(rejection) : resolve(raw);}}))
        : plan?.reject ? Promise.reject(rejection) : Promise.resolve(raw);
      return pending.finally(() => --active);
    }
  };
  class TestDate extends Date {static now() {return clock;}}
  const context = vm.createContext({brookesia: bridge, Date: TestDate});
  vm.runInContext(code, context); vm.runInContext(code, context); // Reopening the JS module must not redeclare global lexical bindings.
  const app = context.brookesia_app;
  return {app, io, labels, values, hidden, timers, started, waiting, context,
    get maxActive() {return maximumActive;}, get active() {return active;},
    count(op) {return counts.get(op) || 0;},
    advance(ms) {clock += ms;}, load(value) {load = value;},
    plan(op, value) {if (!plans.has(op)) plans.set(op, []); plans.get(op).push(value);},
    release(op) {const i = waiting.findIndex(item => !op || item.op === op); assert.ok(i >= 0, 'No deferred request'); waiting.splice(i, 1)[0].finish();},
    guiCount() {return io.filter(item => item.handle === 'SystemGui').length;},
    allLabels() {return [...labels.values()].join('\n');}
  };
}

async function start(f) {assert.equal(f.app.on_start(), true); await flush(); assert.equal(f.timers.size, 1);}
function stop(f) {f.app.on_stop(); assert.equal(f.timers.size, 0); assert.equal(f.started.size, 0); assert.ok(f.maxActive <= 1, 'Resource requests overlapped');}
function visiblePage(f, page) {
  for (const candidate of pages) assert.equal(f.hidden.get('/main/' + candidate), candidate !== page, `Wrong visible page: ${candidate}`);
}

(async () => {
  for (const locale of ['en', 'zh']) {
    const f = fixture(locale); await start(f);
    visiblePage(f, 'overview');
    assert.ok(f.labels.get('/main/overview/cpuText').includes(locales[locale].sampling), 'First CPU sample must say sampling');
    for (const page of pages) {f.app.on_action('resources.' + page); await flush(); visiblePage(f, page);}
    assert.ok(f.allLabels().includes('ESP32-P4'), 'System model is missing');
    assert.ok(f.labels.get('/main/system/details').includes('3.2'), 'Chip revision string was lost');
    assert.ok(f.labels.get('/main/system/details').includes(locales[locale].reset_power_on), 'Reset reason was not translated');
    assert.ok(f.labels.get('/main/storage/sdText').includes(locales[locale].sd_missing), 'Unmounted SD card needs a clear empty state');
    assert.ok(f.count('ResourceStorage') >= 1, 'Storage page did not query storage');
    const oldStorage = f.count('ResourceStorage');
    for (let i = 0; i < 5; ++i) {f.advance(1000); f.app.on_timer(); await flush();}
    assert.equal(f.count('ResourceStorage'), oldStorage, 'Storage queried on every one-second tick');
    assert.equal(f.app.on_action('system.navigation.back'), 'navigation.home'); stop(f);
  }

  const overlap = fixture(); overlap.plan('ResourceSnapshot', {defer: true}); await start(overlap);
  for (let i = 0; i < 5; ++i) {overlap.app.on_timer(); overlap.app.on_action('resources.refresh');}
  overlap.app.on_action('resources.storage'); await flush();
  assert.equal(overlap.count('ResourceSnapshot'), 1); assert.equal(overlap.count('ResourceStorage'), 0);
  overlap.app.on_pause(); assert.equal(overlap.timers.size, 0);
  const pausedCalls = overlap.io.length; overlap.app.on_timer(); await flush(); assert.equal(overlap.io.length, pausedCalls);
  overlap.plan('ResourceSnapshot', {defer: true});
  overlap.app.on_resume(); overlap.app.on_timer(); await flush();
  assert.equal(overlap.count('ResourceSnapshot'), 1, 'Resume duplicated an unresolved sampling request');
  const staleGui = overlap.guiCount(); overlap.release('ResourceSnapshot'); await flush();
  assert.equal(overlap.guiCount(), staleGui, 'A response from before pause updated the resumed GUI');
  assert.equal(overlap.count('ResourceSnapshot'), 2, 'Fresh sampling did not resume after the old request completed');
  overlap.release('ResourceSnapshot'); await flush(); stop(overlap);

  const held = fixture(); await start(held);
  held.app.on_action('resources.hold'); const holdCalls = held.count('ResourceSnapshot');
  for (let i = 0; i < 5; ++i) {held.advance(1000); held.app.on_timer();}
  await flush(); assert.equal(held.count('ResourceSnapshot'), holdCalls, 'Held monitor kept sampling');
  held.app.on_action('resources.hold'); await flush(); assert.ok(held.count('ResourceSnapshot') > holdCalls); stop(held);

  const holdLate = fixture(); await start(holdLate); holdLate.plan('ResourceSnapshot', {defer: true});
  holdLate.advance(1000); holdLate.app.on_timer(); await flush(); holdLate.app.on_action('resources.hold');
  const heldGui = holdLate.guiCount(); holdLate.release(); await flush();
  assert.equal(holdLate.guiCount(), heldGui, 'A response from before Hold changed the frozen view');
  const manual = holdLate.count('ResourceSnapshot'); holdLate.app.on_action('resources.refresh'); await flush();
  assert.equal(holdLate.count('ResourceSnapshot'), manual + 1, 'Manual refresh while held must take one new snapshot');
  assert.equal(holdLate.timers.size, 0); stop(holdLate);

  const late = fixture(); await start(late); late.plan('ResourceSnapshot', {defer: true});
  late.advance(1000); late.app.on_timer(); await flush(); assert.equal(late.waiting.length, 1);
  stop(late); const closed = late.io.length; late.release(); await flush(); assert.equal(late.io.length, closed, 'Late response touched a closed application');
  vm.runInContext(code, late.context); await start({...late, app: late.context.brookesia_app}); late.context.brookesia_app.on_stop();

  const failed = fixture(); failed.plan('ResourceSnapshot', {reject: 'Sample transport disconnected'}); await start(failed);
  assert.match(failed.allLabels(), /Sample transport disconnected/);
  failed.advance(1000); failed.app.on_timer(); await flush(); assert.equal(failed.count('ResourceSnapshot'), 2, 'Failed sample permanently blocked refresh');
  failed.plan('ResourceStorage', {response: JSON.stringify({success: true, data: {ok: false, error: 'Storage unavailable'}})});
  failed.app.on_action('resources.storage'); failed.app.on_action('resources.refresh'); await flush();
  assert.match(failed.allLabels(), /Storage unavailable/); stop(failed);

  const unsupported = fixture(); unsupported.plan('ResourceSnapshot', {response: JSON.stringify({success: true, data: {ok: false, error: 'Unknown operation: ResourceSnapshot'}})});
  assert.equal(unsupported.app.on_start(), true); await flush();
  assert.match(unsupported.allLabels(), /Unknown operation|firmware|固件/i);
  assert.equal(unsupported.timers.size, 0, 'Unsupported old firmware must not be polled indefinitely'); stop(unsupported);

  const legacyPathError = 'Path unavailable (SD missing or protected directory)';
  for (const locale of ['en', 'zh']) {
    const legacy = fixture(locale);
    legacy.plan('ResourceSnapshot', {response: JSON.stringify({success: true, data: {ok: false, error: legacyPathError}})});
    assert.equal(legacy.app.on_start(), true); await flush();
    assert.equal(legacy.labels.get('/main/status'), locales[locale].upgrade, 'The actual old Pocket response must explain the firmware upgrade');
    assert.equal(legacy.timers.size, 0);
    const oldCount = legacy.count('ResourceSnapshot');
    for (let i = 0; i < 5; ++i) legacy.app.on_timer();
    await flush(); assert.equal(legacy.count('ResourceSnapshot'), oldCount, 'Legacy firmware must not be queried forever');
    stop(legacy);

    const legacyStorage = fixture(locale);
    legacyStorage.plan('ResourceStorage', {response: JSON.stringify({success: true, data: {ok: false, error: legacyPathError}})});
    await start(legacyStorage); legacyStorage.app.on_action('resources.storage'); await flush();
    assert.equal(legacyStorage.labels.get('/main/status'), locales[locale].storage_upgrade);
    assert.equal(legacyStorage.labels.get('/main/storage/flashText'), locales[locale].storage_upgrade);
    stop(legacyStorage);

    for (const op of ['ResourceSnapshot', 'ResourceStorage']) {
      const timedOut = fixture(locale);
      timedOut.plan(op, {defer: true, reject: 'Async service function timeout after 5000 ms', rawReject: true});
      await start(timedOut);
      const pendingCount = timedOut.count(op);
      const pendingGui = timedOut.guiCount();
      // A JavaScript-side elapsed clock must not unlock an outstanding native call.
      for (let i = 0; i < 6; ++i) {timedOut.advance(1000); timedOut.app.on_timer(); await flush();}
      assert.equal(timedOut.active, 1, 'Outstanding native request was released by a JS watchdog');
      assert.equal(timedOut.count(op), pendingCount, 'Clock ticks duplicated an outstanding request');
      assert.equal(timedOut.guiCount(), pendingGui, 'A JS watchdog fabricated a native completion');
      timedOut.release(op); await flush();
      assert.ok(timedOut.labels.get('/main/status').includes(locales[locale].timeout), 'Native timeout needs a localized visible explanation');
      assert.equal(timedOut.timers.size, 0, 'Native timeout must pause automatic polling');
      assert.equal(timedOut.active, 0);
      assert.equal(timedOut.labels.get('/main/actions/hold/title'), locales[locale].resume);
      timedOut.app.on_pause(); timedOut.app.on_resume(); await flush();
      assert.ok(timedOut.labels.get('/main/status').includes(locales[locale].timeout), 'Returning from the background hid the timeout explanation');
      assert.equal(timedOut.timers.size, 0, 'Returning from the background resumed timed-out polling');
      const stoppedCounts = timedOut.count('ResourceSnapshot') + timedOut.count('ResourceStorage');
      for (let i = 0; i < 5; ++i) {timedOut.advance(1000); timedOut.app.on_timer(); await flush();}
      assert.equal(timedOut.count('ResourceSnapshot') + timedOut.count('ResourceStorage'), stoppedCounts, 'Timed-out monitor kept retrying automatically');
      timedOut.app.on_action('resources.refresh'); await flush();
      assert.ok(timedOut.count('ResourceSnapshot') + timedOut.count('ResourceStorage') > stoppedCounts, 'Manual retry after native timeout failed');
      assert.equal(timedOut.timers.size, 0, 'Manual retry silently resumed automatic polling');
      timedOut.app.on_action('resources.hold'); await flush();
      assert.equal(timedOut.timers.size, 1, 'Resume did not restore automatic polling');
      stop(timedOut);
    }
  }

  const rawFailure = fixture('zh');
  rawFailure.plan('ResourceSnapshot', {reject: 'Native resource channel disconnected', rawReject: true});
  await start(rawFailure);
  assert.match(rawFailure.labels.get('/main/status'), /Native resource channel disconnected/, 'String rejection was swallowed as undefined');
  rawFailure.advance(1000); rawFailure.app.on_timer(); await flush();
  assert.equal(rawFailure.count('ResourceSnapshot'), 2, 'A native string rejection permanently blocked refresh');
  stop(rawFailure);

  const rawUnsupported = fixture(); rawUnsupported.plan('ResourceSnapshot', {reject: 'Unsupported resource operation', rawReject: true});
  assert.equal(rawUnsupported.app.on_start(), true); await flush();
  assert.equal(rawUnsupported.labels.get('/main/status'), locales.en.upgrade);
  assert.equal(rawUnsupported.timers.size, 0); stop(rawUnsupported);

  const noStats = fixture(); noStats.plan('ResourceSnapshot', {transform(data) {
    data.cpu.error = 'CPU runtime statistics unavailable in this firmware configuration'; return data;
  }});
  await start(noStats); assert.ok(noStats.labels.get('/main/overview/cpuText').includes(locales.en.cpu_unavailable));
  assert.doesNotMatch(noStats.labels.get('/main/overview/cpuText'), /0[.,]0%/); stop(noStats);

  for (const badValue of [null, undefined, NaN]) {
    const unavailableCpu = fixture(); unavailableCpu.plan('ResourceSnapshot', {transform(data) {
      data.cpu.valid = false; data.cpu.loadPercent = badValue; data.cpu.frequencyMHz = badValue;
      for (const core of data.cpu.cores) core.loadPercent = badValue;
      return data;
    }});
    await start(unavailableCpu);
    assert.doesNotMatch(unavailableCpu.labels.get('/main/overview/cpuText'), /0[.,]0%/);
    assert.doesNotMatch(unavailableCpu.labels.get('/main/overview/coreText'), /0[.,]0%/);
    stop(unavailableCpu);
  }

  const duplicate = fixture(); duplicate.load(100); await start(duplicate); duplicate.load(0);
  for (let i = 0; i < 5; ++i) {duplicate.app.on_timer(); await flush();}
  assert.equal(duplicate.values.get('/main/overview/trend/bar29'), 1000, 'A cached timestamp counted as multiple trend samples');
  duplicate.advance(1000); duplicate.app.on_timer(); await flush();
  assert.equal(duplicate.values.get('/main/overview/trend/bar29'), 500, 'Fresh timestamps were not included in the two-second bucket'); stop(duplicate);

  const emptySample = fixture(); await start(emptySample); emptySample.load(100); emptySample.advance(1000); emptySample.app.on_timer(); await flush();
  assert.equal(emptySample.values.get('/main/overview/trend/bar29'), 1000, 'A null CPU sample was counted as zero in a trend average'); stop(emptySample);

  const sdBusy = fixture('zh'); sdBusy.plan('ResourceStorage', {transform(data) {
    data.sd = {mounted: true, available: false, busy: true, total: 0, used: 0, free: 0}; return data;
  }});
  await start(sdBusy); sdBusy.app.on_action('resources.storage'); await flush();
  assert.ok(sdBusy.labels.get('/main/storage/sdText').includes(locales.zh.sd_busy), 'Playback-deferred SD statistics were not explained');
  assert.ok(!sdBusy.labels.get('/main/storage/sdText').includes(locales.zh.sd_missing), 'Busy mounted SD was incorrectly shown as unmounted');
  assert.equal(sdBusy.hidden.get('/main/storage/sdBar'), true, 'Unavailable SD statistics must not show a zero-capacity bar'); stop(sdBusy);

  const trend = fixture(); trend.load(100); await start(trend); trend.load(0);
  for (let i = 0; i < 65; ++i) {trend.advance(1000); trend.app.on_timer(); await flush();}
  const bars = [...trend.values].filter(([current]) => current.startsWith('/main/overview/trend/bar'));
  assert.equal(bars.length, 30, 'Trend must use a bounded set of thirty graph bars');
  assert.ok(bars.every(([, value]) => value === 0), 'Samples older than the last minute stayed in the visible trend');
  stop(trend);
  console.log('PASS Resource Monitor 1.0.1: real manifests/resources/JS, English and Chinese, four pages, one-second serial sampling, nullable CPU, timestamp/cache/trend bounds, storage/SD busy states, translated reset reasons, pause/resume, background/late callbacks, Error and native string rejections, real legacy firmware response, native timeout without overlapping calls, manual retry and resume');
})().catch(error => {console.error(error); process.exitCode = 1;});
