// UI/lifecycle in JS; PCM decoding and I2S output are shared firmware services.
// Keep declarations local: QuickJS reevaluates entry scripts on reopening.
(() => {
  let gui, pocket, audio, timer, timerId, ui;
  let running=false, paused=false, generation=0, session=0, busy=false, polling=false;
  let libraryLoaded=false, requestedRoot='/sdcard/music';
  let root='/sdcard/music', tracks=[], page=0, selected=-1, state='Idle', volume=40;
  let volumeTarget=null, volumeSending=false, pinned='', dragX=null;
  const SD='/sdcard/music', INTERNAL='/littlefs/user/music';
  const errorText=error=>error && typeof error.message==='string'?error.message:String(error);
  const unpack=raw => {
    const r=typeof raw==='string'?JSON.parse(raw):raw;
    if(!r.success) throw new Error(r.error || 'Service failed');
    return r.data;
  };
  const call=(h,n,p) => unpack(brookesia.call_function(h,n,JSON.stringify(p || {})));
  function asyncCall(h,n,p) {
    try {return Promise.resolve(brookesia.call_function_async(h,n,JSON.stringify(p || {}),5000)).then(unpack);}
    catch(error) {return Promise.reject(error);}
  }
  function request(p) {return asyncCall(pocket,'Request',{Request:p}).then(r=>{if(r.ok===false) throw new Error(r.error);return r;});}
  function text(path,value) {if(running && !paused) call(gui,'SetText',{Path:'/main/'+path,Text:String(value)});}
  function status(value) {pinned='';text('player/status',value);}
  function pinStatus(value) {pinned=value;text('player/status',value);}
  function active(token) {return running && !paused && token===generation;}
  function readPayload(raw) {
    if(!raw) return {};
    try {return typeof raw==='string'?JSON.parse(raw):raw;} catch(e) {return {};}
  }
  function shortName(name,limit=15) {
    let result='',units=0;
    for(const character of name) {
      const width=character.charCodeAt(0)<128?0.6:1;
      if(units+width>limit) return result+'…';
      result+=character;units+=width;
    }
    return result;
  }
  function showPlay() {
    if(running && !paused) call(gui,'SetViewSrc',{Path:'/main/player/transport/play/icon',Src:state==='Playing'?'pause':'play'});
  }
  function showPage(library) {
    if(!running || paused) return;
    call(gui,'SetBinding',{Path:'/main/player',Key:'commonProps.hidden',Value:library?'true':'false'});
    call(gui,'SetBinding',{Path:'/main/library',Key:'commonProps.hidden',Value:library?'false':'true'});
  }
  function paintSource() {
    if(!running || paused) return;
    const internal=root===INTERNAL;
    call(gui,'SetBinding',{Path:'/main/library/sources/sd',Key:'style.bgColor',Value:internal?'${color.surface.raised}':'${color.primary.soft}'});
    call(gui,'SetBinding',{Path:'/main/library/sources/internal',Key:'style.bgColor',Value:internal?'${color.primary.soft}':'${color.surface.raised}'});
  }
  function render() {
    text('player/song',selected>=0 && tracks[selected]?shortName(tracks[selected].name,10):ui.library);
    text('library/summary',tracks.length?root+' · '+tracks.length+' '+ui.page:ui.empty);
    text('player/volume_value',ui.volume+' '+volume+'%');
    text('library/pages/number',(page+1)+' / '+Math.max(1,Math.ceil(tracks.length/4)));
    for(let i=0;i<4;i++) {
      const item=tracks[page*4+i];
      if(running && !paused) {
        call(gui,'SetBinding',{Path:'/main/library/track'+i,Key:'commonProps.hidden',Value:item?'false':'true'});
        call(gui,'SetBinding',{Path:'/main/library/track'+i,Key:'style.bgColor',Value:item && page*4+i===selected?'${color.primary.soft}':'${color.surface.raised}'});
      }
      if(item) text('library/track'+i+'/title',shortName(item.name));
    }
    paintSource();
    showPlay();
  }
  function loadRoot(chosen, token, fellBack) {
    const mounted=chosen.startsWith('/sdcard/')?request({Op:'Mount'}):Promise.resolve();
    return mounted.then(()=>{if(!active(token)) return null;return request({Op:'List',Path:chosen});})
      .then(result=>{
        if(!result || !active(token)) return false;
        root=chosen;tracks=result.entries.filter(e=>!e.directory && /\.(mp3|wav)$/i.test(e.name));
        libraryLoaded=true;
        selected=-1;page=0;render();
        if(tracks.length && fellBack) pinStatus(ui.sd_fallback);
        else status(tracks.length?ui.ready:ui.no_tracks);
        return true;
      });
  }
  function refresh(nextRoot, fallbackToInternal) {
    if(pocket==null) {if(nextRoot) root=nextRoot;showLayout();return;}
    if(busy || !running || paused) return;
    const token=++generation;busy=true;status(ui.loading);
    const chosen=nextRoot || root;requestedRoot=chosen;libraryLoaded=false;
    loadRoot(chosen, token, false).catch(e=>{
      if(!active(token)) return false;
      if(fallbackToInternal && chosen!==INTERNAL) {
        status(ui.sd_fallback);
        return loadRoot(INTERNAL, token, true);
      }
      status(errorText(e));return false;
    }).catch(e=>{if(active(token)) status(errorText(e));})
      .finally(()=>{if(token===generation) busy=false;});
  }
  function pumpVolume() {
    if(volumeTarget==null || !running || paused || audio==null) {volumeSending=false;return;}
    const token=session;
    const target=volumeTarget;volumeTarget=null;volumeSending=true;
    asyncCall(audio,'SetVolume',{Volume:target}).catch(e=>{if(running && !paused && token===session) status(errorText(e));})
      .finally(()=>{
        if(!running || token!==session) return;
        if(volumeTarget!=null && !paused) pumpVolume();else volumeSending=false;
      });
  }
  function applyVolume(next) {
    next=Number(next);if(!Number.isFinite(next)) return;
    volume=Math.max(0,Math.min(100,Math.round(next)));
    text('player/volume_value',ui.volume+' '+volume+'%');
    if(audio==null) return;
    volumeTarget=volume;
    if(!volumeSending) pumpVolume();
  }
  function command(name,params) {
    if(busy || !running || paused) return;
    const token=generation;busy=true;
    asyncCall(audio,name,params).then(()=>{if(active(token)) poll(true);})
      .catch(e=>{if(active(token)) status(errorText(e));})
      .finally(()=>{if(token===generation) busy=false;});
  }
  function play(index) {
    if(!tracks.length || busy) return;
    selected=Math.max(0,Math.min(index,tracks.length-1));state='Playing';render();
    if(audio==null) {pinStatus(ui.layout);return;}
    // Native playlist advances even while this application's UI is paused.
    command('PlayUrls',{Urls:tracks.slice(selected).map(e=>'file://'+root+'/'+e.name),Config:{interrupt:true}});
  }
  function poll(force) {
    if(pocket==null || !running || paused || polling || busy && !force) return;
    const token=generation;polling=true;
    request({Op:'MediaState'}).then(s=>{
      if(!active(token)) return;
      state=s.state;const index=tracks.findIndex(e=>root+'/'+e.name===s.path);
      if(index>=0 && index!==selected) {selected=index;page=Math.floor(index/4);render();}
      showPlay();
      if(s.error || state==='Playing' || state==='Paused' || !pinned)
        status(s.error || (!tracks.length?ui.no_tracks:state==='Playing'?ui.playing:state==='Paused'?ui.paused:ui.idle));
      else text('player/status',pinned);
      const seconds=Math.floor((s.positionMs || 0)/1000);
      text('player/elapsed',Math.floor(seconds/60)+':'+String(seconds%60).padStart(2,'0'));
      const total=index>=0?tracks[index].size:0;
      call(gui,'SetValue',{Path:'/main/player/progress',Value:total?Math.min(1000,Math.round((s.bytes || 0)*1000/total)):0});
      return audio==null?null:asyncCall(audio,'GetVolume');
    }).then(v=>{
      if(v==null || !active(token) || volumeSending || volumeTarget!=null) return;
      const next=typeof v==='number'?v:(v.Volume ?? volume);
      if(next===volume) return;
      volume=next;text('player/volume_value',ui.volume+' '+volume+'%');
      call(gui,'SetValue',{Path:'/main/player/slider',Value:volume});
    }).catch(e=>{if(active(token)) status(errorText(e));})
      .finally(()=>{if(token===generation) polling=false;});
  }
  function openService(name) {
    if(typeof brookesia.service_available==='function' && !brookesia.service_available(name)) return null;
    return brookesia.start_service(name);
  }
  function showLayout() {
    tracks=[{name:'pocket-test.wav',size:192512,directory:false},{name:'demo.mp3',size:262144,directory:false}];
    libraryLoaded=true;selected=-1;page=0;render();pinStatus(ui.layout);
  }
  function startTimer() {if(timer!=null) timerId=call(timer,'StartPeriodic',{Name:'music.status',IntervalMs:1000});}
  function stopTimer() {if(timerId!=null) call(timer,'Stop',{TimerId:timerId});timerId=null;}
  globalThis.brookesia_app={
    on_start() {
      running=true;paused=false;busy=polling=false;volumeSending=false;volumeTarget=null;++generation;++session;
      tracks=[];selected=-1;page=0;root=requestedRoot=SD;libraryLoaded=false;state='Idle';pinned='';dragX=null;
      gui=openService('SystemGui');
      if(gui==null) return false;
      pocket=openService('Pocket');audio=openService('AudioPlayback');timer=openService('SystemTimer');
      ui=call(gui,'GetConstant',{Path:'ui'}).Value;
      if(audio!=null) {const savedVolume=call(audio,'GetVolume');volume=typeof savedVolume==='number'?savedVolume:(savedVolume.Volume ?? volume);}
      for(const op of ['previous','play','next','stop','volume','sd','internal','refresh','page_previous','page_next','track0','track1','track2','track3','swipe_player','swipe_library','player_down','player_up','library_down','library_up'])
        call(gui,'SubscribeAction',{Action:'music.'+op});
      call(gui,'SubscribeAction',{Action:'system.navigation.back'});
      render();call(gui,'SetValue',{Path:'/main/player/slider',Value:volume});startTimer();
      if(pocket==null) {root=INTERNAL;showLayout();}
      else refresh(SD, true);
      return true;
    },
    on_action(action, _path, raw) {
      if(action==='system.navigation.back') return 'navigation.home';
      if(!running || paused) return true;
      const payload=readPayload(raw);
      if(action==='music.player_down' || action==='music.library_down') {dragX=Number(payload.x);return true;}
      if(action==='music.player_up' || action==='music.library_up') {
        const start=dragX; dragX=null;
        const dx=Number(payload.x)-start;
        if(start==null || !Number.isFinite(dx)) return true;
        if(action==='music.player_up' && dx<=-36) showPage(true);
        else if(action==='music.library_up' && dx>=36) showPage(false);
        return true;
      }
      if(action==='music.swipe_player') {if(payload.direction==='left') showPage(true);return true;}
      if(action==='music.swipe_library') {if(payload.direction==='right') showPage(false);return true;}
      if(action==='music.volume') {if(payload.value!=null) applyVolume(payload.value);return true;}
      if(busy) return true;
      const op=action.replace('music.','');
      if(op==='play') {
        if(audio==null) {state=state==='Playing'?'Paused':'Playing';if(selected<0 && tracks.length) selected=0;render();pinStatus(ui.layout);}
        else if(state==='Playing') command('Pause');
        else if(state==='Paused') command('Resume');
        else play(selected<0?0:selected);
      } else if(op==='previous') play(Math.max(0,selected-1));
      else if(op==='next') play(selected+1);
      else if(op==='stop') {if(audio==null) {state='Idle';render();pinStatus(ui.layout);} else command('Stop');}
      else if(op==='sd' || op==='internal') refresh(op==='sd'?SD:INTERNAL, op==='sd');
      else if(op==='refresh') refresh(root, root===SD);
      else if(op==='page_previous') {page=Math.max(0,page-1);render();}
      else if(op==='page_next') {page=Math.min(Math.max(0,Math.ceil(tracks.length/4)-1),page+1);render();}
      else if(/^track[0-3]$/.test(op)) {const index=page*4+Number(op.slice(5));if(tracks[index]) play(index);}
      return true;
    },
    on_timer() {poll();return true;},
    on_pause() {paused=true;++generation;busy=polling=false;volumeTarget=null;stopTimer();return true;},
    on_resume() {
      paused=false;++generation;busy=polling=false;startTimer();render();
      if(!libraryLoaded) refresh(requestedRoot,requestedRoot===SD);else poll(true);
      return true;
    },
    on_stop() {
      running=false;++generation;++session;volumeTarget=null;volumeSending=false;
      try {stopTimer();} catch(error) {timerId=null;}
      // Return/Home keeps music playing; closing the recent-task card stops it.
      try {if(audio!=null) call(audio,'Stop');} catch(error) {}
      for(const h of [timer,audio,pocket,gui]) if(h!=null) {
        try {brookesia.stop_service(h);} catch(error) {}
      }
      timer=audio=pocket=gui=null;return true;
    }
  };
})();
