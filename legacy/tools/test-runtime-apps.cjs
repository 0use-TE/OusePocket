// Real application entry scripts, GUI paths and manifests; mock only service I/O.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(name) {
  const src=path.join(root,'examples',name,'src');
  const manifest=JSON.parse(fs.readFileSync(path.join(src,'manifest.json'),'utf8'));
  const screen=JSON.parse(fs.readFileSync(path.join(src,'res/screens/main.json'),'utf8'));
  const ui=JSON.parse(fs.readFileSync(path.join(src,'res/constants/en.json'),'utf8')).data.ui;
  const nodes=new Map(),actions=new Set(),images=new Set(JSON.parse(fs.readFileSync(path.join(src,'res/images/index.json'),'utf8')).images.map(i=>i.id));
  const labels=new Map(),io=[],deferred=[],requests=[];
  function index(node,parent='') {const p=parent+'/'+node.id;nodes.set(p,node);for(const e of node.events||[]) actions.add(e.action);for(const c of node.children||[]) index(c,p);}
  index(screen);
  let noSD=false,missing=false,error='',media={ok:true,path:'',state:'Idle',positionMs:0,error:''},nextLog=1;
  const unavailable=new Set();
  let pendingOperation=null,rejectedOperation=null,rejectedValue=null;
  function operation(handle,name,p) {
    io.push({handle,name,p});let data=null;
    if(handle==='SystemGui') {
      if(name==='SetText') {assert.equal(nodes.get(p.Path)?.type,'label',p.Path);labels.set(p.Path,p.Text);}
      else if(name==='SetBinding') assert.ok(nodes.get(p.Path)?.bindings?.[p.Key],`Missing binding ${p.Path} ${p.Key}`);
      else if(name==='SetViewSrc') {assert.equal(nodes.get(p.Path)?.type,'image');assert.ok(images.has(p.Src));}
      else if(name==='SubscribeAction') assert.ok(actions.has(p.Action)||p.Action==='system.navigation.back',p.Action);
      else if(name==='GetConstant') data={Value:ui};
    } else if(handle==='Pocket') {
      const r=p.Request;
      if(r.Op==='Mount') data=noSD?{ok:false,error:'No SD card'}:{ok:true};
      else if(r.Op==='List') data=missing?{ok:false,error:'Music folder missing'}:{ok:true,entries:[
        {name:'song.mp3',directory:false},{name:'demo.WAV',directory:false},{name:'notes.txt',directory:false},{name:'album.mp3',directory:true}]};
      else if(r.Op==='MediaState') data={...media,error};
      else if(r.Op==='LogsRead') data={ok:true,entries:[{sequence:nextLog,text:'I (1) Ready\nE (2) failed test\n中文日志 🎵\n'}],last:nextLog++,persistent:true};
      else if(r.Op==='LogsSaved') data={ok:true,text:'E (9) saved from previous boot\n'};
      else data={ok:true};
    } else if(handle==='AudioPlayback') {
      if(name==='GetVolume') data=55;
      else if(name==='PlayUrls') {assert.equal(p.Urls.length,2);media={...media,path:p.Urls[0].slice(7),state:'Playing'};}
      else if(name==='Pause') media.state='Paused';
      else if(name==='Resume') media.state='Playing';
      else if(name==='Stop') media.state='Idle';
      else if(name==='SetVolume') assert.ok(p.Volume>=0 && p.Volume<=100);
    } else if(handle==='SystemTimer' && name==='StartPeriodic') data=42;
    return JSON.stringify({success:true,data});
  }
  const b={service_available(service){return !unavailable.has(service);},start_service(service){
    if(unavailable.has(service)) throw new Error('Service not found: '+service);
    if(!['SystemGui','SystemCore','SystemTimer'].includes(service)) assert.ok(manifest.services.some(d=>d.name===service),`Undeclared ${service}`);
    return service;
  },stop_service(h){io.push({handle:h,name:'release'});},
  call_function(h,n,raw){return operation(h,n,JSON.parse(raw));},
  call_function_async(h,n,raw){const p=JSON.parse(raw);
    requests.push({handle:h,name:n,p});
    if(rejectedOperation===p.Request?.Op) {rejectedOperation=null;return Promise.reject(rejectedValue);}
    if(pendingOperation===p.Request?.Op) {pendingOperation=null;return new Promise(resolve=>deferred.push(()=>resolve(operation(h,n,p))));}
    return Promise.resolve(operation(h,n,p));
  }};
  const ctx=vm.createContext({brookesia:b});const code=fs.readFileSync(path.join(src,'app/main.js'),'utf8');
  vm.runInContext(code,ctx);vm.runInContext(code,ctx); // Exactly the old redeclaration failure.
  return {app:ctx.brookesia_app,io,labels,ui,ctx,code,actions,requests,
    noSD(v){noSD=v;},missing(v){missing=v;},error(v){error=v;},delay(op){pendingOperation=op;},
    reject(op,value){rejectedOperation=op;rejectedValue=value;},
    without(names){for(const name of names) unavailable.add(name);},deferred,
    count(name){return io.filter(i=>i.name===name && (name!=='Stop' || i.handle==='AudioPlayback')).length;},op(name){return io.filter(i=>i.p?.Request?.Op===name);}};
}
(async()=>{
  const m=fixture('music');m.noSD(true);assert.equal(m.app.on_start(),true);await tick();
  assert.match(m.labels.get('/main/player/status'),/internal/i);
  assert.match(m.labels.get('/main/library/summary'),/littlefs/);
  m.noSD(false);m.missing(true);m.app.on_action('music.refresh');await tick();assert.match(m.labels.get('/main/player/status'),/missing/);
  m.missing(false);m.app.on_action('music.refresh');await tick();assert.match(m.labels.get('/main/library/summary'),/2/);
  m.app.on_action('music.sd');await tick();assert.match(m.labels.get('/main/library/summary'),/sdcard/);
  m.app.on_action('music.player_down','','{"x":220,"y":40}');
  m.app.on_action('music.player_up','','{"x":80,"y":42}');
  assert.equal(m.io.findLast(i=>i.name==='SetBinding' && i.p.Path==='/main/library').p.Value,'false');
  m.app.on_action('music.library_down','','{"x":40,"y":40}');
  m.app.on_action('music.library_up','','{"x":160,"y":40}');
  assert.equal(m.io.findLast(i=>i.name==='SetBinding' && i.p.Path==='/main/library').p.Value,'true');
  m.app.on_action('music.swipe_player','','{"direction":"left"}');
  assert.equal(m.io.findLast(i=>i.name==='SetBinding' && i.p.Path==='/main/library').p.Value,'false');
  m.app.on_action('music.track0');await tick();assert.equal(m.count('PlayUrls'),1);assert.equal(m.labels.get('/main/player/song'),'song.mp3');
  m.app.on_action('music.play');await tick();assert.equal(m.count('Pause'),1);
  m.app.on_action('music.play');await tick();assert.equal(m.count('Resume'),1);
  m.app.on_action('music.volume','','{"value":70}');await tick();assert.equal(m.io.findLast(i=>i.name==='SetVolume').p.Volume,70);
  const stops=m.count('Stop');m.app.on_pause();m.app.on_timer();await tick();assert.equal(m.count('Stop'),stops,'Backgrounding must keep music playing');
  m.app.on_resume();await tick();m.error('SD removed');m.app.on_timer();await tick();assert.equal(m.labels.get('/main/player/status'),'SD removed');
  assert.equal(m.app.on_action('system.navigation.back'),'navigation.home');
  m.delay('List');m.app.on_action('music.refresh');await tick();m.app.on_stop();
  const guiBefore=m.count('SetText');m.deferred.shift()();await tick();assert.equal(m.count('SetText'),guiBefore,'Late music response touched closed GUI');
  assert.equal(m.count('Stop'),stops+1);vm.runInContext(m.code,m.ctx);assert.equal(m.ctx.brookesia_app.on_start(),true);await tick();m.ctx.brookesia_app.on_stop();
  const preview=fixture('music');preview.without(['Pocket','AudioPlayback','SystemTimer']);
  assert.equal(preview.app.on_start(),true);
  assert.match(preview.labels.get('/main/player/status'),/layout only/);
  assert.match(preview.labels.get('/main/library/summary'),/2/);
  preview.app.on_action('music.swipe_player','','{"direction":"left"}');
  assert.equal(preview.io.findLast(i=>i.name==='SetBinding' && i.p.Path==='/main/library').p.Value,'false');
  preview.app.on_action('music.track0');assert.equal(preview.labels.get('/main/player/song'),'pocket-test.wav');
  assert.equal(preview.count('PlayUrls'),0);assert.equal(preview.count('SetVolume'),0);
  preview.app.on_action('music.volume','','{"value":70}');assert.equal(preview.labels.get('/main/player/volume_value'),'Volume 70%');
  preview.app.on_action('music.internal');assert.match(preview.labels.get('/main/library/summary'),/littlefs/);
  preview.app.on_stop();assert.equal(preview.io.filter(i=>i.name==='Stop').length,0);
  const l=fixture('log-viewer');l.app.on_start();await tick();assert.match(l.labels.get('/main/body/text'),/中文日志 🎵/);
  l.app.on_action('logs.filter');assert.match(l.labels.get('/main/body/text'),/failed/);assert.doesNotMatch(l.labels.get('/main/body/text'),/Ready/);
  l.app.on_action('logs.clear');const reads=l.op('LogsRead').length;l.app.on_timer();await tick();assert.equal(l.op('LogsClear').length,0);assert.equal(l.op('LogsRead').length,reads);
  l.app.on_action('logs.clear');await tick();assert.equal(l.op('LogsClear').length,1);assert.equal(l.labels.get('/main/body/text'),'');
  l.app.on_action('logs.saved');await tick();assert.match(l.labels.get('/main/body/text'),/previous boot/);
  l.app.on_action('logs.live');await tick();l.app.on_action('logs.hold');const old=l.op('LogsRead').length;l.app.on_timer();await tick();assert.equal(l.op('LogsRead').length,old);
  l.app.on_action('logs.hold');await tick();l.app.on_pause();const before=l.op('LogsRead').length;l.app.on_timer();await tick();assert.equal(l.op('LogsRead').length,before);
  l.app.on_resume();await tick();l.delay('LogsRead');l.app.on_timer();l.app.on_stop();const count=l.count('SetText');l.deferred.shift()();await tick();assert.equal(l.count('SetText'),count);
  const failures=fixture('log-viewer');failures.reject('LogsRead','Pocket: read timed out');
  failures.app.on_start();await tick();assert.equal(failures.labels.get('/main/status'),'Pocket: read timed out');
  failures.reject('LogsSaved','LittleFS unavailable');failures.app.on_action('logs.saved');await tick();
  assert.equal(failures.labels.get('/main/status'),'LittleFS unavailable');
  failures.app.on_action('logs.clear');failures.reject('LogsClear','Flash write failed');failures.app.on_action('logs.clear');await tick();
  assert.equal(failures.labels.get('/main/status'),'Flash write failed');
  failures.app.on_stop();
  const flight=fixture('log-viewer');flight.app.on_start();await tick();
  const timers=flight.count('StartPeriodic');flight.app.on_start();
  assert.equal(flight.count('StartPeriodic'),timers,'Repeated start leaked a polling timer');
  flight.delay('LogsRead');flight.app.on_timer();flight.app.on_action('logs.hold');
  const frozen=flight.count('SetText');flight.deferred.shift()();await tick();
  assert.equal(flight.count('SetText'),frozen,'An in-flight response changed the held log view');
  const heldRequests=flight.requests.length;flight.app.on_pause();flight.app.on_resume();await tick();
  assert.equal(flight.requests.length,heldRequests,'Resuming bypassed the held log view');
  assert.equal(flight.labels.get('/main/status'),flight.ui.paused);
  flight.app.on_action('logs.hold');await tick();flight.delay('LogsRead');flight.app.on_timer();
  const sent=flight.requests.length;
  flight.app.on_action('logs.saved');flight.app.on_action('logs.live');flight.app.on_pause();flight.app.on_resume();
  assert.equal(flight.requests.length,sent,'Mode changes/resume overlapped an in-flight request');
  flight.deferred.shift()();await tick();assert.equal(flight.requests.length,sent+1,'The latest view was not refreshed after the old response');
  assert.equal(flight.requests.at(-1).p.Request.Op,'LogsRead');
  flight.delay('LogsRead');flight.app.on_timer();flight.app.on_action('logs.saved');flight.app.on_stop();
  const closed=flight.requests.length;flight.deferred.shift()();await tick();
  assert.equal(flight.requests.length,closed,'Queued refresh used services after close');
  console.log('PASS real Music/Logs scripts: declared services, every GUI path, reopen without redeclarations, MP3/WAV list, errors, controls, background playback, timers, late callbacks, log history/filter/clear confirmation');
})().catch(e=>{console.error(e);process.exitCode=1;});
