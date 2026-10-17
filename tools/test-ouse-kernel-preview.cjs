// Exercise the actual preview bindings without a browser or network.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, 'vscode-ouse-bpk/media/preview.js'), 'utf8');
const bindings = source.slice(source.indexOf('  function previewTaskSnapshot('), source.indexOf('  const previewConsole ='));
const lifecycle = source.slice(source.indexOf('    background: {'), source.indexOf('    notifications: {'));
const context = vm.createContext({ queueMicrotask, Date, JSON, TextEncoder, console });
vm.runInContext(`
  let previewInstance=1,previewTaskId=0,taskHandler=null,paused=false,homes=false;
  let backgroundAllowed=true,backgroundActive=false,backgroundReason='',backgroundSince=0,lifecycleHandler=null;
  const previewTasks=[],privateStore=new Map(),previewAlarms=new Map(),timers=[],started=Date.now();
  let networkAllowed=true,alarmsAllowed=true,mediaAllowed=true,notificationsAllowed=true;
  const audio={recordedMs:0,positionMs:0},wifi={enabled:false,connected:false,networks:[],reason:0,ssid:'',ip:''};
  function utf8Length(value){return new TextEncoder().encode(value).length;}
  function showFault(error){throw error;}
  function resumeTimers(){} function pauseTimers(){} function notify(){}
  const Ouse={
    ${lifecycle}
    system:{},storage:{},
    audio:{get:()=>({...audio}),testTone:()=>true,record:()=>true,playRecording:()=>true,stop:()=>true},
    wifi:{get:()=>({...wifi}),setEnabled:(v)=>{wifi.enabled=v;return true;},scan:()=>true,connect:()=>true,disconnect:()=>true,forget:()=>true}
  };
  ${bindings}
  globalThis.test={Ouse,privateStore,pause(){homes=true;paused=true;},
    reset(){previewInstance++;previewTasks.length=0;privateStore.clear();taskHandler=null;homes=false;paused=false;}};
`, context);
const api = context.test.Ouse;
const tick = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  const events = [];
  api.tasks.onComplete(task => events.push(task));
  context.test.pause();
  const write = api.storage.set('x', { value: 1 });
  assert.equal(typeof write.taskId, 'number');
  await tick();
  assert.equal(events.length, 0);
  assert.equal(api.tasks.get(write.taskId).state, 'succeeded');
  assert.equal(api.background.begin('preview-test'), true);
  assert.equal(api.background.get().reason, 'preview-test');
  assert.equal(await write, true);
  assert.equal(events.length, 1);
  assert.equal(events[0].result.ok, true);
  const read = api.storage.get('x');
  assert.equal((await read).value, 1);
  const cancelled = api.storage.info();
  const caught = cancelled.catch(error => error.message);
  assert.equal(api.tasks.cancel(cancelled.taskId), true);
  assert.equal(await caught, 'Cancelled');
  assert.equal(api.tasks.get(cancelled.taskId).state, 'cancelled');
  assert.equal(api.system.diagnostics().budgetClock, 'preview-simulated');
  assert.equal(api.system.tasks(0, 1).length, 1);
  assert.throws(() => api.tasks.list(0, 17));
  api.background.end();
  assert.equal(api.background.get().runnable, false);
  let staleDelivered = false;
  const stale = api.storage.set('late', 123);
  stale.then(() => { staleDelivered = true; });
  context.test.reset();
  await tick();
  assert.equal(staleDelivered, false);
  assert.equal(context.test.privateStore.has('late'), false);
  const tone = api.audio.testTone(), stop = api.audio.stop();
  await tick();
  assert.equal(api.tasks.get(tone).state, 'cancelled');
  assert.equal(api.tasks.get(stop).state, 'succeeded');
  assert.equal(api.system.capabilities().network.responseBodyMax, 32768);
  assert.equal(api.system.capabilities().media.available, false);
  assert.equal(api.clock.get().source, 'preview-simulated');
  const response = await api.net.request({url: 'https://device-only.invalid/'});
  assert.equal(response.simulated, true);
  assert.equal(JSON.parse(response.body).simulated, true);
  const alarm = api.alarms.set({id: 'probe', title: 'Preview', atMs: Date.now() + 10000});
  assert.equal(typeof alarm, 'number');await tick();
  assert.equal(api.alarms.list().length, 1);
  api.alarms.remove('probe');await tick();assert.equal(api.alarms.list().length, 0);
  context.test.pause();
  const pending = Array.from({ length: 8 }, () => api.storage.info());
  const overflow = api.storage.info().catch(error => error.code);
  await tick();
  assert.equal(await overflow, 'BUSY');
  api.background.begin('drain');
  await Promise.all(pending);
  console.log('PASS preview: task IDs/results, suspended delivery, cancellation, background reason, paging, reload isolation, bounded pending work');
})().catch(error => { console.error(error); process.exitCode = 1; });
