// Execute the shipped Music entry. Only the native service boundary is replaced.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
const source=path.join(root,'examples/music/src');
const code=fs.readFileSync(path.join(source,'app/main.js'),'utf8');
const ui=JSON.parse(fs.readFileSync(path.join(source,'res/constants/en.json'),'utf8')).data.ui;
const tick=()=>new Promise(resolve=>setImmediate(resolve));

function fixture() {
  const calls=[],labels=new Map(),handles=new Map(),pending=[],deferred=[],failures=[];
  let nextHandle=0,nextTimer=0,media={ok:true,path:'',state:'Idle',positionMs:0,bytes:0,error:''};
  const timers=new Set();
  function record(handle,name,raw,timeout,async) {
    assert.ok(handles.has(handle),`Unknown service handle: ${String(handle)}`);
    assert.ok(!handles.get(handle).released,`Used released service: ${handle}`);
    const params=JSON.parse(raw),service=handles.get(handle).service;
    const operation=service==='Pocket'?params.Request.Op:name;
    const call={handle,service,name,params,timeout,async,operation};calls.push(call);
    return call;
  }
  function response(call) {
    const {service,name,params}=call;let data=null;
    if(service==='SystemGui') {
      if(name==='GetConstant') data={Value:ui};
      else if(name==='SetText') labels.set(params.Path,params.Text);
      else if(name==='SetValue') assert.ok(Number.isFinite(params.Value),'Non-finite GUI value');
    } else if(service==='Pocket') {
      if(params.Request.Op==='List') data={ok:true,entries:[
        {name:'one.mp3',size:256,directory:false},
        {name:'two.WAV',size:512,directory:false},
        {name:'notes.txt',size:32,directory:false},
      ]};
      else if(params.Request.Op==='MediaState') data={...media};
      else data={ok:true};
    } else if(service==='AudioPlayback') {
      if(name==='GetVolume') data=55;
      else if(name==='PlayUrls') {
        assert.ok(params.Urls.length>0);
        media={...media,path:params.Urls[0].slice(7),state:'Playing'};
      } else if(name==='Pause') media.state='Paused';
      else if(name==='Resume') media.state='Playing';
      else if(name==='Stop') media={...media,state:'Idle'};
      else if(name==='SetVolume') assert.ok(Number.isFinite(params.Volume) && params.Volume>=0 && params.Volume<=100);
    } else if(service==='SystemTimer') {
      if(name==='StartPeriodic') {data=++nextTimer;timers.add(data);}
      else if(name==='Stop') {assert.ok(timers.delete(params.TimerId),'Stopped missing timer');}
    }
    return JSON.stringify({success:true,data});
  }
  const bridge={
    service_available(){return true;},
    start_service(service) {const handle=service+':'+(++nextHandle);handles.set(handle,{service,released:false});return handle;},
    stop_service(handle) {
      assert.ok(handles.has(handle) && !handles.get(handle).released,'Service double release');
      handles.get(handle).released=true;calls.push({handle,service:handles.get(handle).service,name:'release'});
    },
    call_function(handle,name,raw) {
      const call=record(handle,name,raw,undefined,false);
      const index=failures.findIndex(f=>!f.async && f.operation===call.operation && (!f.service || f.service===call.service));
      if(index>=0) throw failures.splice(index,1)[0].error;
      return response(call);
    },
    call_function_async(handle,name,raw,timeout) {
      const call=record(handle,name,raw,timeout,true);
      const index=failures.findIndex(f=>f.async && f.operation===call.operation);
      if(index>=0) {
        const failure=failures.splice(index,1)[0];
        if(failure.throws) throw failure.error;
        return Promise.reject(failure.error);
      }
      const wait=deferred.indexOf(call.operation);
      if(wait>=0) {
        deferred.splice(wait,1);
        return new Promise((resolve,reject)=>pending.push({
          call,complete(){resolve(response(call));},reject,
        }));
      }
      return Promise.resolve(response(call));
    },
  };
  const context=vm.createContext({brookesia:bridge});
  vm.runInContext(code,context);
  return {app:context.brookesia_app,calls,labels,handles,pending,timers,
    defer(operation){deferred.push(operation);},
    reject(operation,error){failures.push({operation,error,async:true});},
    throwAsync(operation,error){failures.push({operation,error,async:true,throws:true});},
    throwSync(operation,error){failures.push({operation,error,async:false,service:'AudioPlayback'});},
    count(operation){return calls.filter(c=>c.operation===operation).length;},
    volume(value){return context.brookesia_app.on_action('music.volume','',JSON.stringify({value}));},
    async start(){assert.equal(context.brookesia_app.on_start(),true);await tick();},
    assertReleased(){assert.equal([...handles.values()].filter(h=>!h.released).length,0);assert.equal(timers.size,0);},
  };
}

(async()=>{
  const errors=fixture();await errors.start();
  errors.reject('PlayUrls','Native audio request timed out');
  errors.app.on_action('music.track0');await tick();
  assert.equal(errors.labels.get('/main/player/status'),'Native audio request timed out');
  errors.reject('List','Internal directory unavailable');
  errors.app.on_action('music.internal');await tick();
  assert.equal(errors.labels.get('/main/player/status'),'Internal directory unavailable');
  errors.throwAsync('MediaState','Native bridge unavailable');
  assert.doesNotThrow(()=>errors.app.on_timer());await tick();
  assert.equal(errors.labels.get('/main/player/status'),'Native bridge unavailable');
  const reads=errors.count('MediaState');errors.app.on_timer();await tick();
  assert.equal(errors.count('MediaState'),reads+1,'Synchronous bridge error wedged polling');
  assert.ok(errors.calls.filter(c=>c.async).every(c=>c.timeout===5000),'Native requests need bounded timeouts');
  errors.app.on_stop();errors.assertReleased();

  const library=fixture();library.defer('List');await library.start();
  assert.equal(library.pending.length,1);library.app.on_pause();
  library.pending.shift().complete();await tick();
  library.app.on_resume();await tick();
  assert.equal(library.count('List'),2,'Returning during initial loading must retry the discarded directory request');
  assert.match(library.labels.get('/main/library/summary'),/2/);
  library.app.on_stop();library.assertReleased();

  const sourceSwitch=fixture();await sourceSwitch.start();sourceSwitch.defer('List');
  sourceSwitch.app.on_action('music.internal');await tick();sourceSwitch.app.on_pause();
  sourceSwitch.pending.shift().complete();await tick();sourceSwitch.app.on_resume();await tick();
  assert.match(sourceSwitch.labels.get('/main/library/summary'),/littlefs/,
    'Resuming must finish the requested library switch');
  sourceSwitch.app.on_stop();sourceSwitch.assertReleased();

  const background=fixture();await background.start();background.defer('SetVolume');
  background.volume(70);background.volume(80);assert.equal(background.count('SetVolume'),1);
  background.app.on_pause();background.app.on_resume();await tick();
  background.volume(90);
  assert.equal(background.count('SetVolume'),1,'Pause/resume must not duplicate an in-flight volume request');
  background.pending.shift().complete();await tick();
  const backgroundVolumes=background.calls.filter(c=>c.operation==='SetVolume').map(c=>c.params.Volume);
  assert.deepEqual(backgroundVolumes,[70,90],'Volume queue must retain only the latest foreground target');
  assert.equal(background.calls.filter(c=>c.service==='AudioPlayback' && c.name==='Stop').length,0,
    'Backgrounding must preserve native playback');
  background.volume('not a number');assert.equal(background.count('SetVolume'),2);
  background.app.on_stop();background.assertReleased();

  const closed=fixture();await closed.start();closed.defer('SetVolume');
  closed.volume(70);closed.volume(80);closed.app.on_stop();closed.assertReleased();
  const writes=closed.calls.length;closed.pending.shift().complete();await tick();
  assert.equal(closed.calls.length,writes,'Late volume completion called released services');

  const reopened=fixture();await reopened.start();reopened.defer('SetVolume');
  reopened.volume(70);reopened.volume(80);reopened.app.on_stop();
  await reopened.start();reopened.defer('SetVolume');reopened.volume(20);reopened.volume(30);
  assert.equal(reopened.count('SetVolume'),2);
  reopened.pending.shift().complete();await tick();
  assert.equal(reopened.count('SetVolume'),2,'Previous session drained the new session volume queue');
  reopened.pending.shift().complete();await tick();
  assert.deepEqual(reopened.calls.filter(c=>c.operation==='SetVolume').map(c=>c.params.Volume),[70,20,30]);
  reopened.app.on_stop();reopened.assertReleased();

  const stopped=fixture();await stopped.start();stopped.app.on_action('music.track0');await tick();
  stopped.app.on_stop();await stopped.start();
  const pauses=stopped.count('Pause');stopped.app.on_action('music.play');await tick();
  assert.equal(stopped.count('Pause'),pauses,'Reopening kept stale Playing state');
  assert.equal(stopped.count('PlayUrls'),2);
  stopped.throwSync('Stop','Native Stop rejected');
  assert.doesNotThrow(()=>stopped.app.on_stop());stopped.assertReleased();

  const cycles=fixture();
  for(let index=0;index<20;index++) {
    await cycles.start();cycles.app.on_pause();cycles.app.on_resume();await tick();
    cycles.app.on_stop();cycles.assertReleased();
  }
  console.log('PASS real Music lifecycle: native string errors/timeouts, resumed directory load/source, volume serialization/close/reopen, release after Stop failure, 20 timer/service cleanup cycles');
})().catch(error=>{console.error(error);process.exitCode=1;});
