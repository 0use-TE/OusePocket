(() => {
  let gui,pocket,timer,timerId,ui;
  let running=false,paused=false,held=false,busy=false,generation=0,cursor=0;
  let mode='live',filter=false,clearArmed=false,content='';
  function unpack(raw) {const r=typeof raw==='string'?JSON.parse(raw):raw;if(!r.success) throw new Error(r.error || 'Service failed');return r.data;}
  const call=(h,n,p)=>unpack(brookesia.call_function(h,n,JSON.stringify(p || {})));
  function request(p) {return brookesia.call_function_async(pocket,'Request',JSON.stringify({Request:p})).then(unpack).then(r=>{if(r.ok===false) throw new Error(r.error);return r;});}
  function active(token) {return running && !paused && token===generation;}
  function text(path,value) {if(running && !paused) call(gui,'SetText',{Path:'/main/'+path,Text:String(value)});}
  function status(value) {text('status',value);}
  function render() {
    const visible=filter?content.split('\n').filter(line=>/^E \(/.test(line) || /\b(error|failed|panic|abort)\b/i.test(line)).join('\n'):content;
    text('body/text',visible);text('modes/filter/title',filter?ui.all:ui.filter);
    text('actions/hold/title',held?ui.resume:ui.hold);
  }
  function tail(value) {let result=Array.from(value).slice(-5000).join('');if(result.length<value.length){const i=result.indexOf('\n');if(i>=0) result=result.slice(i+1);}return result;}
  function refresh(force) {
    if(!running || paused || busy || held && !force) return;
    const token=generation;busy=true;
    const op=mode==='saved'?{Op:'LogsSaved'}:{Op:'LogsRead',After:cursor};
    request(op).then(result=>{
      if(!active(token)) return;
      if(mode==='saved') {content=tail(result.text || '');status(ui.saved_hint);}
      else {
        content=tail(content+(result.entries || []).map(e=>e.text).join(''));
        cursor=result.last;if(!clearArmed) status(result.missed?ui.missed:result.persistent===false?ui.not_persistent:ui.live);
      }
      render();
    }).catch(e=>{if(active(token)) status(e.message);})
      .finally(()=>{if(token===generation) busy=false;});
  }
  function startTimer() {timerId=call(timer,'StartPeriodic',{Name:'logs.poll',IntervalMs:1000});}
  function stopTimer() {if(timerId!=null) call(timer,'Stop',{TimerId:timerId});timerId=null;}
  globalThis.brookesia_app={
    on_start() {
      running=true;paused=false;held=false;busy=false;cursor=0;mode='live';content='';filter=false;clearArmed=false;++generation;
      gui=brookesia.start_service('SystemGui');pocket=brookesia.start_service('Pocket');timer=brookesia.start_service('SystemTimer');
      ui=call(gui,'GetConstant',{Path:'ui'}).Value;
      for(const op of ['live','saved','filter','hold','refresh','clear']) call(gui,'SubscribeAction',{Action:'logs.'+op});
      call(gui,'SubscribeAction',{Action:'system.navigation.back'});startTimer();render();refresh();return true;
    },
    on_action(action) {
      if(action==='system.navigation.back') return 'navigation.home';
      if(!running || paused) return true;
      const op=action.replace('logs.','');
      if(op!=='clear') clearArmed=false;
      if(op==='live' || op==='saved') {++generation;busy=false;mode=op;content='';cursor=0;render();refresh(true);}
      else if(op==='filter') {filter=!filter;render();}
      else if(op==='hold') {held=!held;render();if(!held) refresh();else status(ui.paused);}
      else if(op==='refresh') refresh(true);
      else if(op==='clear') {
        if(!clearArmed) {clearArmed=true;status(ui.confirm);return true;}
        if(busy) return true;
        clearArmed=false;busy=true;const token=++generation;
        request({Op:'LogsClear'}).then(()=>{if(active(token)) {content='';cursor=0;render();status(ui.cleared);}})
          .catch(e=>{if(active(token)) status(e.message);}).finally(()=>{if(token===generation) busy=false;});
      }
      return true;
    },
    on_timer() {if(mode==='live' && !clearArmed) refresh();return true;},
    on_pause() {paused=true;clearArmed=false;++generation;busy=false;stopTimer();return true;},
    on_resume() {paused=false;++generation;busy=false;startTimer();render();refresh(true);return true;},
    on_stop() {running=false;++generation;stopTimer();for(const h of [timer,pocket,gui]) if(h!=null) brookesia.stop_service(h);timer=pocket=gui=null;return true;}
  };
})();
