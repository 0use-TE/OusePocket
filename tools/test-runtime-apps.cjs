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
  const labels=new Map(),io=[],deferred=[];
  function index(node,parent='') {const p=parent+'/'+node.id;nodes.set(p,node);for(const e of node.events||[]) actions.add(e.action);for(const c of node.children||[]) index(c,p);}
  index(screen);
  let noSD=false,missing=false,error='',media={ok:true,path:'',state:'Idle',positionMs:0,error:''},nextLog=1;
  let pendingOperation=null;
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
  const b={start_service(service){
    if(!['SystemGui','SystemCore','SystemTimer'].includes(service)) assert.ok(manifest.services.some(d=>d.name===service),`Undeclared ${service}`);
    return service;
  },stop_service(h){io.push({handle:h,name:'release'});},
  call_function(h,n,raw){return operation(h,n,JSON.parse(raw));},
  call_function_async(h,n,raw){const p=JSON.parse(raw);
    if(pendingOperation===p.Request?.Op) {pendingOperation=null;return new Promise(resolve=>deferred.push(()=>resolve(operation(h,n,p))));}
    return Promise.resolve(operation(h,n,p));
  }};
  const ctx=vm.createContext({brookesia:b});const code=fs.readFileSync(path.join(src,'app/main.js'),'utf8');
  vm.runInContext(code,ctx);vm.runInContext(code,ctx); // Exactly the old redeclaration failure.
  return {app:ctx.brookesia_app,io,labels,ui,ctx,code,actions,
    noSD(v){noSD=v;},missing(v){missing=v;},error(v){error=v;},delay(op){pendingOperation=op;},deferred,
    count(name){return io.filter(i=>i.name===name && (name!=='Stop' || i.handle==='AudioPlayback')).length;},op(name){return io.filter(i=>i.p?.Request?.Op===name);}};
}
(async()=>{
  const m=fixture('music');m.noSD(true);assert.equal(m.app.on_start(),true);await tick();
  assert.match(m.labels.get('/main/status'),/No SD/);
  m.noSD(false);m.missing(true);m.app.on_action('music.refresh');await tick();assert.match(m.labels.get('/main/status'),/missing/);
  m.missing(false);m.app.on_action('music.refresh');await tick();assert.match(m.labels.get('/main/library'),/2/);
  m.app.on_action('music.track0');await tick();assert.equal(m.count('PlayUrls'),1);assert.equal(m.labels.get('/main/song'),'song.mp3');
  m.app.on_action('music.play');await tick();assert.equal(m.count('Pause'),1);
  m.app.on_action('music.play');await tick();assert.equal(m.count('Resume'),1);
  m.app.on_action('music.volume_up');await tick();assert.equal(m.io.findLast(i=>i.name==='SetVolume').p.Volume,60);
  const stops=m.count('Stop');m.app.on_pause();m.app.on_timer();await tick();assert.equal(m.count('Stop'),stops,'Backgrounding must keep music playing');
  m.app.on_resume();await tick();m.error('SD removed');m.app.on_timer();await tick();assert.equal(m.labels.get('/main/status'),'SD removed');
  assert.equal(m.app.on_action('system.navigation.back'),'navigation.home');
  m.delay('List');m.app.on_action('music.refresh');await tick();m.app.on_stop();
  const guiBefore=m.count('SetText');m.deferred.shift()();await tick();assert.equal(m.count('SetText'),guiBefore,'Late music response touched closed GUI');
  assert.equal(m.count('Stop'),stops+1);vm.runInContext(m.code,m.ctx);assert.equal(m.ctx.brookesia_app.on_start(),true);await tick();m.ctx.brookesia_app.on_stop();
  const l=fixture('log-viewer');l.app.on_start();await tick();assert.match(l.labels.get('/main/body/text'),/中文日志 🎵/);
  l.app.on_action('logs.filter');assert.match(l.labels.get('/main/body/text'),/failed/);assert.doesNotMatch(l.labels.get('/main/body/text'),/Ready/);
  l.app.on_action('logs.clear');const reads=l.op('LogsRead').length;l.app.on_timer();await tick();assert.equal(l.op('LogsClear').length,0);assert.equal(l.op('LogsRead').length,reads);
  l.app.on_action('logs.clear');await tick();assert.equal(l.op('LogsClear').length,1);assert.equal(l.labels.get('/main/body/text'),'');
  l.app.on_action('logs.saved');await tick();assert.match(l.labels.get('/main/body/text'),/previous boot/);
  l.app.on_action('logs.live');await tick();l.app.on_action('logs.hold');const old=l.op('LogsRead').length;l.app.on_timer();await tick();assert.equal(l.op('LogsRead').length,old);
  l.app.on_action('logs.hold');await tick();l.app.on_pause();const before=l.op('LogsRead').length;l.app.on_timer();await tick();assert.equal(l.op('LogsRead').length,before);
  l.app.on_resume();await tick();l.delay('LogsRead');l.app.on_timer();l.app.on_stop();const count=l.count('SetText');l.deferred.shift()();await tick();assert.equal(l.count('SetText'),count);
  console.log('PASS real Music/Logs scripts: declared services, every GUI path, reopen without redeclarations, MP3/WAV list, errors, controls, background playback, timers, late callbacks, log history/filter/clear confirmation');
})().catch(e=>{console.error(e);process.exitCode=1;});
