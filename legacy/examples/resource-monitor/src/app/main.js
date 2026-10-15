// Optional JSON / JS app. Metrics come from Pocket; no native busy loop or UI thread sampling.
// The entry is evaluated again on reopening, so all declarations stay local.
(() => {
  let gui, pocket, timer, timerId, ui;
  let running=false, paused=false, held=false, generation=0, inFlight=false;
  let needSnapshot=false, needStorage=false, snapshotUnsupported=false, storageUnsupported=false;
  let view='overview', snapshot=null, storage=null, lastStorageAt=0, storageAttempted=false, history=[];
  let snapshotError='', storageError='';
  const displayed=new Map();
  const views=['overview','memory','storage','system'];
  const requestTimeoutMs=5000;
  const errorText=error=>error && typeof error.message==='string'?error.message:String(error);
  function unpack(raw) {
    const value=typeof raw==='string'?JSON.parse(raw):raw;
    if(!value || !value.success) throw new Error(value && value.error || 'Service failed');
    return value.data;
  }
  const call=(handle,name,params)=>unpack(brookesia.call_function(handle,name,JSON.stringify(params || {})));
  function request(op,token) {
    return Promise.resolve().then(()=>{
      if(!active(token)) throw new Error('Request superseded');
      return brookesia.call_function_async(pocket,'Request',JSON.stringify({Request:{Op:op}}),requestTimeoutMs);
    })
      .then(unpack).then(value=>{
        if(!value || value.ok===false) throw new Error(value && value.error || 'Empty resource response');
        if(value.schemaVersion!==1) throw new Error('Unsupported resource schema');
        return value;
      });
  }
  function active(token) {return running && !paused && token===generation;}
  function update(name,path,value) {
    if(!running || paused) return;
    const key=name+':'+path, previous=displayed.get(key);
    if(previous===value) return;
    const params={Path:'/main/'+path};
    if(name==='SetText') params.Text=String(value);
    else if(name==='SetValue') params.Value=value;
    else {params.Key='hidden';params.Value=value?'true':'false';}
    call(gui,name,params);displayed.set(key,value);
  }
  const text=(path,value)=>update('SetText',path,String(value));
  const bar=(path,value)=>update('SetValue',path,Math.max(0,Math.min(1000,Math.round(value || 0))));
  const hidden=(path,value)=>update('SetBinding',path,value);
  const number=value=>typeof value==='number' && Number.isFinite(value);
  function bytes(value) {
    if(!number(value) || value<0) return ui.unavailable;
    if(value<1024) return Math.round(value)+' B';
    if(value<1048576) return (value/1024).toFixed(1)+' KiB';
    return (value/1048576).toFixed(2)+' MiB';
  }
  function percent(value) {return number(value)?Math.max(0,Math.min(100,value)).toFixed(1)+'%':ui.sampling;}
  function memoryRatio(value) {return value && number(value.total) && value.total>0?Math.max(0,Math.min(1,value.used/value.total)):0;}
  function memorySummary(name,value) {
    if(!value || !number(value.total) || value.total<=0) return name+' · '+ui.unavailable;
    return name+'\n'+ui.used+': '+bytes(value.used)+' / '+bytes(value.total)+' ('+(100*memoryRatio(value)).toFixed(1)+'%)';
  }
  function memoryDetails(value) {
    if(!value || !number(value.total) || value.total<=0) return ui.unavailable;
    return ui.used+': '+bytes(value.used)+' / '+bytes(value.total)+' ('+(100*memoryRatio(value)).toFixed(1)+'%)'+
      '\n'+ui.free+': '+bytes(value.free)+'\n'+ui.minimum_free+': '+bytes(value.minimumFree)+
      '\n'+ui.largest+': '+bytes(value.largestFree);
  }
  function fileSummary(name,value) {
    if(!value || !value.available) return name+'\n'+ui.unavailable;
    return memorySummary(name,value)+'\n'+ui.free+': '+bytes(value.free);
  }
  function duration(milliseconds) {
    let seconds=Math.floor(Math.max(0,milliseconds || 0)/1000);
    const days=Math.floor(seconds/86400);seconds%=86400;
    const hours=Math.floor(seconds/3600);seconds%=3600;
    const minutes=Math.floor(seconds/60);seconds%=60;
    return (days?days+ui.days+' ':'')+hours+ui.hours+' '+minutes+ui.minutes+' '+seconds+ui.seconds;
  }
  function cpuValue(cpu) {
    if(cpu && cpu.valid && number(cpu.loadPercent)) return percent(cpu.loadPercent);
    return cpu && (cpu.error || cpu.intervalMs>0)?ui.cpu_unavailable:ui.sampling;
  }
  function appendHistory(value) {
    const captured=value.capturedAtMs;
    if(!number(captured)) return;
    const previous=history[history.length-1];
    if(previous && previous.at===captured) return;
    if(previous && previous.at>captured) history=[];
    const cpu=value.cpu;
    history.push({at:captured,load:cpu && cpu.valid && number(cpu.loadPercent)?Math.max(0,Math.min(100,cpu.loadPercent)):null});
    history=history.filter(item=>captured-item.at<60000).slice(-60);
  }
  function renderTrend() {
    const sums=Array(30).fill(0), counts=Array(30).fill(0);
    const latest=history.length?history[history.length-1].at:0;
    for(const item of history) {
      const index=29-Math.floor((latest-item.at)/2000);
      if(index>=0 && index<30 && number(item.load)) {sums[index]+=item.load;counts[index]++;}
    }
    for(let i=0;i<30;i++) bar('overview/trend/bar'+i,counts[i]?sums[i]*10/counts[i]:0);
  }
  function renderOverview() {
    if(snapshot) {
      const cpu=snapshot.cpu || {}, memory=snapshot.memory || {};
      text('overview/cpuText',ui.cpu+': '+cpuValue(cpu));
      const cores=Array.isArray(cpu.cores)?cpu.cores:[];
      text('overview/coreText',cores.map(core=>ui.core+' '+core.id+': '+(cpu.valid?percent(core.loadPercent):cpuValue(cpu))).join(' · ')+
        '\n'+ui.frequency+': '+(number(cpu.frequencyMHz)?cpu.frequencyMHz+' MHz':ui.unavailable));
      text('overview/internalText',memorySummary(ui.internal_ram,memory.internal));
      text('overview/psramText',memorySummary(ui.psram,memory.psram));
      bar('overview/internalBar',1000*memoryRatio(memory.internal));
      bar('overview/psramBar',1000*memoryRatio(memory.psram));
      renderTrend();
    }
    if(storage) {
      text('overview/storageText',fileSummary(ui.internal_storage,storage.internal));
      bar('overview/storageBar',storage.internal && storage.internal.available?1000*memoryRatio(storage.internal):0);
      hidden('overview/storageBar',!storage.internal || !storage.internal.available);
    } else if(storageUnsupported) text('overview/storageText',ui.storage_upgrade);
  }
  function renderMemory() {
    if(!snapshot) return;
    const memory=snapshot.memory || {};
    text('memory/internalText',memoryDetails(memory.internal));text('memory/psramText',memoryDetails(memory.psram));
    bar('memory/internalBar',1000*memoryRatio(memory.internal));bar('memory/psramBar',1000*memoryRatio(memory.psram));
  }
  function renderStorage() {
    if(!storage) {text('storage/flashText',storageUnsupported?ui.storage_upgrade:ui.sampling);return;}
    const flash=storage.flash || {}, firmware=storage.firmware || {}, sd=storage.sd || {};
    text('storage/flashText',ui.flash+': '+bytes(flash.totalBytes));
    text('storage/internalText',fileSummary(ui.internal_storage,storage.internal));
    bar('storage/internalBar',storage.internal && storage.internal.available?1000*memoryRatio(storage.internal):0);
    hidden('storage/internalBar',!storage.internal || !storage.internal.available);
    text('storage/sdText',sd.busy?ui.sd+'\n'+ui.sd_busy:sd.mounted?fileSummary(ui.sd,sd):ui.sd+'\n'+ui.sd_missing);
    bar('storage/sdBar',sd.available?1000*memoryRatio(sd):0);hidden('storage/sdBar',!sd.available);
    text('storage/firmwareText',ui.firmware+': '+(firmware.projectName || '—')+' '+(firmware.version || '')+
      '\n'+ui.firmware_image+': '+bytes(firmware.imageBytes)+
      '\n'+ui.firmware_partition+': '+(firmware.partition || '—')+' · '+bytes(firmware.partitionBytes));
    text('storage/partitionText',(Array.isArray(flash.partitions)?flash.partitions:[]).map(partition=>
      partition.label+' ['+partition.type+']\n'+bytes(partition.size)+' · 0x'+Math.max(0,partition.offset).toString(16)).join('\n\n') || ui.unavailable);
  }
  function renderSystem() {
    if(!snapshot) return;
    const system=snapshot.system || {}, cpu=snapshot.cpu || {};
    text('system/details',ui.chip+': '+(system.chipModel || '—')+'\n'+ui.revision+': '+(system.chipRevision || '—')+
      '\n'+ui.cores+': '+(system.cores ?? '—')+'\n'+ui.frequency+': '+(number(cpu.frequencyMHz)?cpu.frequencyMHz+' MHz':ui.unavailable)+
      '\n'+ui.uptime+': '+duration(snapshot.uptimeMs)+'\n'+ui.reset+': '+(ui['reset_'+system.resetReason] || system.resetReason || '—')+
      '\n'+ui.version+': '+(system.firmwareVersion || '—')+'\n'+ui.idf+': '+(system.idfVersion || '—')+
      '\n'+ui.sample_time+': '+(number(snapshot.sampleDurationUs)?(snapshot.sampleDurationUs/1000).toFixed(2)+' ms':ui.unavailable)+
      '\n'+ui.interval+': '+(number(cpu.intervalMs)?cpu.intervalMs+' ms':ui.unavailable));
  }
  function render() {
    if(!running || paused) return;
    for(const name of views) {
      hidden(name,name!==view);
      text((name==='overview' || name==='memory'?'tabsTop/':'tabsBottom/')+name+'/title',(name===view?'> ':'')+ui[name]);
    }
    text('actions/hold/title',held?ui.resume:ui.hold);
    // Only the visible view receives metric updates.
    if(view==='overview') renderOverview();
    else if(view==='memory') renderMemory();
    else if(view==='storage') renderStorage();
    else renderSystem();
  }
  function status(value) {text('status',value);}
  function renderStatus() {
    status(snapshotUnsupported?ui.upgrade:snapshotError?ui.error+': '+snapshotError:
      (view==='overview' || view==='storage') && storageError?ui.error+': '+storageError:
      (view==='overview' || view==='storage') && storageUnsupported?ui.storage_upgrade:held?ui.paused:ui.live);
  }
  function unsupported(message,op) {
    // Older Pocket builds route these new operations through file-path validation.
    const legacyResourceResponse=(op==='ResourceSnapshot' || op==='ResourceStorage') &&
      message==='Path unavailable (SD missing or protected directory)';
    return legacyResourceResponse || /unknown.*op|unsupported|not supported|not implemented|不支持/i.test(message);
  }
  function drain() {
    if(!running || paused || inFlight) return;
    const op=needSnapshot && !snapshotUnsupported?'ResourceSnapshot':needStorage && !storageUnsupported?'ResourceStorage':null;
    if(!op) return;
    const token=generation;
    if(op==='ResourceSnapshot') needSnapshot=false;
    else {needStorage=false;storageAttempted=true;lastStorageAt=Date.now();}
    inFlight=true;
    request(op,token).then(value=>{
      if(!active(token)) return;
      if(op==='ResourceSnapshot') {snapshot=value;snapshotError='';appendHistory(value);}
      else {storage=value;storageError='';}
      render();renderStatus();
    }).catch(error=>{
      if(!active(token)) return;
      const message=errorText(error);
      if(unsupported(message,op)) {
        if(op==='ResourceSnapshot') {snapshotUnsupported=true;needSnapshot=false;stopTimer();status(ui.upgrade);}
        else {storageUnsupported=true;needStorage=false;render();status(ui.storage_upgrade);}
      } else {
        const timedOut=/\btimeout\b|timed\s*out|超时/i.test(message);
        if(timedOut) {
          // The native bridge has completed this Promise. Do not use a separate JS
          // watchdog that could release inFlight while native work is outstanding.
          held=true;needSnapshot=needStorage=false;stopTimer();
        }
        if(op==='ResourceSnapshot') snapshotError=timedOut?ui.timeout:message;
        else storageError=timedOut?ui.timeout:message;
        if(timedOut) render();
        renderStatus();
      }
    }).finally(()=>{
      // A pause / resume must not clear this lock while the native call is outstanding.
      inFlight=false;
      if(running && !paused) drain();
    });
  }
  function refresh(force) {
    if(!running || paused || held && !force) return;
    if(!snapshotUnsupported) needSnapshot=true;
    if((view==='overview' || view==='storage') && !storageUnsupported &&
       (force || !storageAttempted || Date.now()-lastStorageAt>=10000)) needStorage=true;
    if(force) status(snapshotUnsupported?ui.upgrade:ui.loading);
    drain();
  }
  function startTimer() {
    if(timerId==null && !held && !snapshotUnsupported)
      timerId=call(timer,'StartPeriodic',{Name:'resources.poll',IntervalMs:1000});
  }
  function stopTimer() {if(timerId!=null) call(timer,'Stop',{TimerId:timerId});timerId=null;}
  globalThis.brookesia_app={
    on_start() {
      running=true;paused=held=false;++generation;view='overview';snapshot=storage=null;history=[];
      needSnapshot=needStorage=snapshotUnsupported=storageUnsupported=false;lastStorageAt=0;storageAttempted=false;displayed.clear();
      snapshotError=storageError='';
      gui=brookesia.start_service('SystemGui');pocket=brookesia.start_service('Pocket');timer=brookesia.start_service('SystemTimer');
      ui=call(gui,'GetConstant',{Path:'ui'}).Value;
      for(const action of [...views,'hold','refresh']) call(gui,'SubscribeAction',{Action:'resources.'+action});
      call(gui,'SubscribeAction',{Action:'system.navigation.back'});
      render();startTimer();refresh(true);return true;
    },
    on_action(action) {
      if(action==='system.navigation.back') return 'navigation.home';
      if(!running || paused) return true;
      const name=action.replace('resources.','');
      if(views.includes(name)) {
        view=name;
        if(view!=='overview' && view!=='storage') needStorage=false;
        render();renderStatus();
        if(!held) refresh(false);
      } else if(name==='hold') {
        held=!held;++generation;needSnapshot=needStorage=false;
        if(held) {stopTimer();status(ui.paused);} else {startTimer();refresh(true);}
        render();
      } else if(name==='refresh') refresh(true);
      return true;
    },
    on_timer() {refresh(false);return true;},
    on_pause() {paused=true;++generation;needSnapshot=needStorage=false;stopTimer();return true;},
    on_resume() {
      paused=false;++generation;render();
      if(!held) {startTimer();refresh(true);}else renderStatus();
      return true;
    },
    on_stop() {
      running=false;++generation;needSnapshot=needStorage=false;stopTimer();
      for(const handle of [timer,pocket,gui]) if(handle!=null) brookesia.stop_service(handle);
      timer=pocket=gui=null;history=[];displayed.clear();return true;
    }
  };
})();
