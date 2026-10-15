// UI/lifecycle in JS; PCM decoding and I2S output are shared firmware services.
// Keep declarations local: QuickJS reevaluates entry scripts on reopening.
(() => {
  let gui, pocket, audio, timer, timerId, ui;
  let running=false, paused=false, generation=0, busy=false, polling=false;
  let root='/sdcard/music', tracks=[], page=0, selected=-1, state='Idle', volume=40;
  const unpack=raw => {
    const r=typeof raw==='string'?JSON.parse(raw):raw;
    if(!r.success) throw new Error(r.error || 'Service failed');
    return r.data;
  };
  const call=(h,n,p) => unpack(brookesia.call_function(h,n,JSON.stringify(p || {})));
  const asyncCall=(h,n,p) => brookesia.call_function_async(h,n,JSON.stringify(p || {})).then(unpack);
  function request(p) {return asyncCall(pocket,'Request',{Request:p}).then(r=>{if(r.ok===false) throw new Error(r.error);return r;});}
  function text(path,value) {if(running && !paused) call(gui,'SetText',{Path:'/main/'+path,Text:String(value)});}
  function status(value) {text('status',value);}
  function active(token) {return running && !paused && token===generation;}
  function shortName(name) {
    let result='',units=0;
    for(const character of name) {
      const width=character.charCodeAt(0)<128?0.6:1;
      if(units+width>15) return result+'...';
      result+=character;units+=width;
    }
    return result;
  }
  function showPlay() {
    if(running && !paused) call(gui,'SetViewSrc',{Path:'/main/controls/play/icon',Src:state==='Playing'?'pause':'play'});
  }
  function render() {
    text('song',selected>=0 && tracks[selected]?tracks[selected].name:ui.library);
    text('library',tracks.length?root+' · '+tracks.length+' '+ui.page:ui.empty);
    text('volume/value',ui.volume+' '+volume+'%');
    text('pages/number',(page+1)+' / '+Math.max(1,Math.ceil(tracks.length/4)));
    for(let i=0;i<4;i++) {
      const item=tracks[page*4+i];
      if(running && !paused) call(gui,'SetBinding',{Path:'/main/track'+i,Key:'commonProps.hidden',Value:item?'false':'true'});
      if(item) text('track'+i+'/title',(page*4+i===selected?'> ':'')+shortName(item.name));
    }
    showPlay();
  }
  function refresh(nextRoot) {
    if(busy || !running || paused) return;
    const token=++generation;busy=true;status(ui.loading);
    const chosen=nextRoot || root;
    const mounted=chosen.startsWith('/sdcard/')?request({Op:'Mount'}):Promise.resolve();
    mounted.then(()=>{if(!active(token)) return null;return request({Op:'List',Path:chosen});})
      .then(result=>{
        if(!result || !active(token)) return;
        root=chosen;tracks=result.entries.filter(e=>!e.directory && /\.(mp3|wav)$/i.test(e.name));
        selected=-1;page=0;render();status(tracks.length?ui.ready:ui.no_tracks);
      }).catch(e=>{if(active(token)) status(e.message);})
      .finally(()=>{if(token===generation) busy=false;});
  }
  function command(name,params) {
    if(busy || !running || paused) return;
    const token=generation;busy=true;
    asyncCall(audio,name,params).then(()=>{if(active(token)) poll(true);})
      .catch(e=>{if(active(token)) status(e.message);})
      .finally(()=>{if(token===generation) busy=false;});
  }
  function play(index) {
    if(!tracks.length || busy) return;
    selected=Math.max(0,Math.min(index,tracks.length-1));state='Playing';render();
    // Native playlist advances even while this application's UI is paused.
    command('PlayUrls',{Urls:tracks.slice(selected).map(e=>'file://'+root+'/'+e.name),Config:{interrupt:true}});
  }
  function poll(force) {
    if(!running || paused || polling || busy && !force) return;
    const token=generation;polling=true;
    request({Op:'MediaState'}).then(s=>{
      if(!active(token)) return;
      state=s.state;const index=tracks.findIndex(e=>root+'/'+e.name===s.path);
      if(index>=0 && index!==selected) {selected=index;page=Math.floor(index/4);render();}
      showPlay();status(s.error || (!tracks.length?ui.no_tracks:state==='Playing'?ui.playing:state==='Paused'?ui.paused:ui.idle));
      const seconds=Math.floor((s.positionMs || 0)/1000);
      text('elapsed',ui.elapsed+' '+Math.floor(seconds/60)+':'+String(seconds%60).padStart(2,'0'));
      const total=index>=0?tracks[index].size:0;
      call(gui,'SetValue',{Path:'/main/progress',Value:total?Math.min(1000,Math.round((s.bytes || 0)*1000/total)):0});
      return asyncCall(audio,'GetVolume');
    }).then(v=>{if(v!=null && active(token)) {volume=typeof v==='number'?v:(v.Volume ?? volume);text('volume/value',ui.volume+' '+volume+'%');}})
      .catch(e=>{if(active(token)) status(e.message);})
      .finally(()=>{if(token===generation) polling=false;});
  }
  function startTimer() {timerId=call(timer,'StartPeriodic',{Name:'music.status',IntervalMs:1000});}
  function stopTimer() {if(timerId!=null) call(timer,'Stop',{TimerId:timerId});timerId=null;}
  globalThis.brookesia_app={
    on_start() {
      running=true;paused=false;busy=polling=false;++generation;tracks=[];selected=-1;page=0;
      gui=brookesia.start_service('SystemGui');pocket=brookesia.start_service('Pocket');
      audio=brookesia.start_service('AudioPlayback');timer=brookesia.start_service('SystemTimer');
      ui=call(gui,'GetConstant',{Path:'ui'}).Value;
      const savedVolume=call(audio,'GetVolume');volume=typeof savedVolume==='number'?savedVolume:savedVolume.Volume;
      for(const op of ['previous','play','next','stop','volume_down','volume_up','sd','internal','refresh','page_previous','page_next','track0','track1','track2','track3'])
        call(gui,'SubscribeAction',{Action:'music.'+op});
      call(gui,'SubscribeAction',{Action:'system.navigation.back'});
      render();startTimer();refresh();return true;
    },
    on_action(action) {
      if(action==='system.navigation.back') return 'navigation.home';
      if(!running || paused || busy) return true;
      const op=action.replace('music.','');
      if(op==='play') {
        if(state==='Playing') command('Pause');
        else if(state==='Paused') command('Resume');
        else play(selected<0?0:selected);
      } else if(op==='previous') play(Math.max(0,selected-1));
      else if(op==='next') play(selected+1);
      else if(op==='stop') command('Stop');
      else if(op==='volume_up' || op==='volume_down') {volume=Math.max(0,Math.min(100,volume+(op==='volume_up'?5:-5)));render();command('SetVolume',{Volume:volume});}
      else if(op==='sd' || op==='internal') refresh(op==='sd'?'/sdcard/music':'/littlefs/user/music');
      else if(op==='refresh') refresh();
      else if(op==='page_previous') {page=Math.max(0,page-1);render();}
      else if(op==='page_next') {page=Math.min(Math.max(0,Math.ceil(tracks.length/4)-1),page+1);render();}
      else if(/^track[0-3]$/.test(op)) {const index=page*4+Number(op.slice(5));if(tracks[index]) play(index);}
      return true;
    },
    on_timer() {poll();return true;},
    on_pause() {paused=true;++generation;busy=polling=false;stopTimer();return true;},
    on_resume() {paused=false;++generation;busy=polling=false;startTimer();render();poll(true);return true;},
    on_stop() {
      running=false;++generation;stopTimer();
      // Return/Home keeps music playing; closing the recent-task card stops it.
      if(audio!=null) call(audio,'Stop');
      for(const h of [timer,audio,pocket,gui]) if(h!=null) brookesia.stop_service(h);
      timer=audio=pocket=gui=null;return true;
    }
  };
})();
