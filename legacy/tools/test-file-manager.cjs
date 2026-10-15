// Exercise application behavior with the real JS module and mocked service I/O.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const code = fs.readFileSync(require('node:path').join(__dirname, '../examples/file-manager/src/app/main.js'), 'utf8');
const source = require('node:path').join(__dirname, '../examples/file-manager/src');
const manifest = JSON.parse(fs.readFileSync(source + '/manifest.json', 'utf8'));
const screen = JSON.parse(fs.readFileSync(source + '/res/screens/main.json', 'utf8'));
const nodes = new Map();
function indexNodes(node, parent = '') {
  const path = parent + '/' + node.id;
  nodes.set(path, node);
  for (const child of node.children || []) indexNodes(child, path);
}
indexNodes(screen);
const io = [], labels = new Map();
let unavailable = false, keyboard = 0, deferred = null;
const dirs = {
  '/littlefs/user': [{name:'Documents', directory:true, size:0}, {name:'note.txt', directory:false, size:5}],
  '/littlefs/user/Documents': []
};
const brookesia = {
  start_service(name) {
    if (!['SystemGui', 'SystemCore', 'SystemTimer'].includes(name)) {
      const requirement = (manifest.services || []).find(service => service.name === name);
      assert.ok(requirement, `Service unavailable: ${name} (service is not declared in app manifest)`);
      assert.equal(requirement.version, '1.0.0');
    }
    return name;
  },
  stop_service: name => io.push(['stop', name]), subscribe_event: () => {},
  call_function(handle, name, params) {
    const p = JSON.parse(params); io.push([handle, name, p]);
    let data = null;
    if(name === 'SetText') {
      assert.equal(nodes.get(p.Path)?.type, 'label', `Missing GUI text label: ${p.Path}`);
      labels.set(p.Path, p.Text);
    }
    if(name === 'SetBinding') {
      assert.ok(Object.values(nodes.get(p.Path)?.bindings || {}).includes(p.Key), `Missing binding: ${p.Path}/${p.Key}`);
    }
    if(name === 'ShowKeyboard') data = {RequestId:++keyboard};
    if(name === 'StartPeriodic') data = 1;
    return JSON.stringify({success:true, data});
  },
  async call_function_async(handle, name, params) {
    const p = JSON.parse(params).Request; io.push(['request', p]);
    if(deferred && p.Op === 'List') await deferred;
    let data;
    if(p.Op === 'Mount') data = {ok:!unavailable, error:'No SD'};
    else if(p.Op === 'List') data = {ok:true, entries:dirs[p.Path] || [], limited:false};
    else if(p.Op === 'Volumes') data = {ok:true, internalTotal:1024, internalUsed:512};
    else if(p.Op === 'CopyStatus') data = {busy:false, bytes:0, total:0};
    else data = {ok:true};
    return JSON.stringify({success:true, data});
  }
};
const context = {brookesia, console};
vm.createContext(context); vm.runInContext(code, context);
const app = context.brookesia_app;
const flush = () => new Promise(resolve => setImmediate(resolve));
const operations = op => io.filter(x => x[0] === 'request' && x[1].Op === op);
(async () => {
  assert.equal(app.on_start(), true); await flush();
  assert.equal(labels.get('/main/path'), '/littlefs/user');
  unavailable = true; app.on_action('files.sd'); await flush();
  assert.equal(labels.get('/main/path'), '/littlefs/user');
  assert.match(labels.get('/main/status'), /No SD/);
  app.on_action('files.entry0'); app.on_action('files.entry0'); await flush();
  assert.equal(labels.get('/main/path'), '/littlefs/user/Documents');
  app.on_action('system.navigation.back'); await flush();
  assert.equal(labels.get('/main/path'), '/littlefs/user');
  assert.equal(app.on_action('system.navigation.back'), 'navigation.home');
  app.on_action('files.entry1'); app.on_action('files.delete'); await flush();
  assert.equal(operations('Remove').length, 0);
  app.on_action('files.delete'); await flush();
  assert.equal(operations('Remove').length, 1);
  app.on_action('files.mkdir');
  app.on_event('SystemCore', 'KeyboardClosed', JSON.stringify({RequestId:keyboard+1, Confirmed:true, Text:'ignored'}));
  assert.equal(operations('Mkdir').length, 0);
  app.on_event('SystemCore', 'KeyboardClosed', JSON.stringify({RequestId:keyboard, Confirmed:true, Text:'../bad'}));
  assert.equal(operations('Mkdir').length, 0);
  app.on_action('files.mkdir');
  app.on_event('SystemCore', 'KeyboardClosed', JSON.stringify({RequestId:keyboard, Confirmed:true, Text:'New folder'}));
  await flush(); assert.equal(operations('Mkdir').at(-1)[1].Path, '/littlefs/user/New folder');
  app.on_pause(); const oldCount = io.length; app.on_timer(); await flush(); assert.equal(io.length, oldCount);
  app.on_resume(); await flush();
  let release;
  deferred = new Promise(resolve => { release=resolve; });
  app.on_action('files.refresh'); app.on_stop();
  const afterStop = io.length; release(); await flush();
  assert.equal(io.length, afterStop, 'Late response must not touch a stopped GUI');
  console.log('PASS: no SD, directory/back, delete confirmation, keyboard scoping/name validation, pause/resume, late response');
})().catch(error => { console.error(error); process.exitCode=1; });
