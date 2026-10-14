// UI and navigation live in this independently replaceable JavaScript package.
let gui, pocket, core, timer;
let path = '/littlefs/user', entries = [], page = 0, selected = null, clipboard = null;
let keyboardId = null, pendingName = null, deleteArmed = null;
let running = false, paused = false, generation = 0, pollId = null;
let copyPending = false;
function unpack(raw) {
  const result = typeof raw === 'string' ? JSON.parse(raw) : raw;
  if (!result.success) throw new Error(result.error || 'Service failed');
  return result.data;
}
function call(handle, name, params) {
  return unpack(brookesia.call_function(handle, name, JSON.stringify(params || {})));
}
function text(id, value) { call(gui, 'SetText', {Path:'/main/' + id, Text:String(value)}); }
function message(value) { if (running && !paused) text('status', value); }
function request(data) {
  return brookesia.call_function_async(pocket, 'Request', JSON.stringify({Request:data})).then(unpack).then(value => {
    if (value.ok === false) throw new Error(value.error || 'SD unavailable');
    return value;
  });
}
function render() {
  text('path', path);
  for (let i = 0; i < 6; i++) {
    const item = entries[page * 6 + i];
    call(gui, 'SetBinding', {Path:'/main/entry' + i, Key:'hidden', Value:item ? 'false' : 'true'});
    if (item) text('entry' + i + '/title', (item.directory ? '[DIR] ' : '') + item.name);
  }
}
function refresh() {
  if (!running || paused) return;
  const token = ++generation;
  request({Op:'List', Path:path}).then(result => {
    if (!running || paused || token !== generation) return;
    entries = result.entries; page = Math.min(page, Math.max(0, Math.ceil(entries.length / 6) - 1));
    render(); message(entries.length + ' entries' + (result.limited ? ' (first 512)' : ''));
  }).catch(error => { if (running && !paused && token === generation) { entries=[]; render(); message(error.message); } });
}
function back() {
  selected = null; deleteArmed = null;
  const root = path.startsWith('/sdcard') ? '/sdcard' : '/littlefs/user';
  if (path === root) return false;
  path = path.slice(0, path.lastIndexOf('/')); page=0; refresh();
  return true;
}
function askName(op) {
  pendingName = {op, path, selected};
  const result = call(core, 'ShowKeyboard', {Options:{title:op === 'Mkdir' ? 'New folder' : 'Rename',
    initial_text:op === 'Rename' && selected ? selected.name : '', max_length:64}});
  keyboardId = result.RequestId;
}
function mutate(data) {
  request(data).then(() => { if (running) { selected=null; refresh(); } }).catch(e => message(e.message));
}
globalThis.brookesia_app = {
  on_start() {
    running=true; paused=false; path='/littlefs/user'; entries=[]; selected=null; page=0; clipboard=null; copyPending=false;
    gui=brookesia.start_service('SystemGui'); pocket=brookesia.start_service('Pocket');
    core=brookesia.start_service('SystemCore'); timer=brookesia.start_service('SystemTimer');
    const actions=['internal','sd','back','previous','next','mkdir','rename','copy','paste','delete','refresh','eject'];
    for (let i=0;i<6;i++) actions.push('entry'+i);
    for (const action of actions) call(gui,'SubscribeAction',{Action:'files.'+action});
    call(gui,'SubscribeAction',{Action:'system.navigation.back'});
    brookesia.subscribe_event(core,'KeyboardClosed');
    pollId=call(timer,'StartPeriodic',{Name:'files.copy',IntervalMs:1000});
    render(); refresh();
    request({Op:'Volumes'}).then(v => message('Internal: '+Math.round(v.internalUsed/1024)+' / '+Math.round(v.internalTotal/1024)+' KiB')).catch(e=>message(e.message));
    return true;
  },
  on_action(action) {
    if (action==='system.navigation.back') return back() ? true : 'navigation.home';
    const op=action.replace('files.','');
    deleteArmed = op==='delete' ? deleteArmed : null;
    if (op==='back') return back() ? true : 'navigation.home';
    if (op==='internal') { path='/littlefs/user'; selected=null; page=0; refresh(); }
    else if (op==='sd') request({Op:'Mount'}).then(() => { if(running) { path='/sdcard'; page=0; selected=null; refresh(); } }).catch(e=>message(e.message));
    else if (op==='eject') request({Op:'Eject'}).then(() => { if(running) { path='/littlefs/user'; page=0; refresh(); message('SD safely removed'); } }).catch(e=>message(e.message));
    else if (op==='previous') { page=Math.max(0,page-1); render(); }
    else if (op==='next') { page=Math.min(Math.max(0,Math.ceil(entries.length/6)-1),page+1); render(); }
    else if (op.startsWith('entry')) {
      const item=entries[page*6+Number(op.slice(5))];
      if (item && selected && selected.name===item.name && item.directory) { path += '/'+item.name; page=0; selected=null; refresh(); }
      else if(item) { selected=item; message('Selected: '+item.name+' ('+item.size+' bytes)'+(item.directory?' — tap again to open':'')); }
    } else if (op==='mkdir') askName('Mkdir');
    else if (op==='rename' && selected) askName('Rename');
    else if (op==='copy' && selected && !selected.directory) { clipboard=path+'/'+selected.name; message('Copied selection. Open destination and Paste.'); }
    else if (op==='paste' && clipboard) request({Op:'Copy',Path:clipboard,To:path+'/'+clipboard.slice(clipboard.lastIndexOf('/')+1)}).then(()=>{copyPending=true; message('Copy started');}).catch(e=>message(e.message));
    else if (op==='delete' && selected) {
      const target=path+'/'+selected.name;
      if(deleteArmed===target) { deleteArmed=null; mutate({Op:'Remove',Path:target}); }
      else { deleteArmed=target; message('Tap Delete again to confirm: '+selected.name); }
    } else if(op==='refresh') refresh();
    return true;
  },
  on_event(service, event, json) {
    if(service!=='SystemCore' || event!=='KeyboardClosed') return true;
    const values=JSON.parse(json);
    if(values.RequestId!==keyboardId) return true;
    keyboardId=null;
    const pending=pendingName; pendingName=null;
    if(!values.Confirmed || !pending) return true;
    const name=values.Text.trim();
    if(!name || name==='.' || name==='..' || /[\\/\x00-\x1f]/.test(name)) { message('Invalid name'); return true; }
    const data={Op:pending.op,Path:pending.path+'/'+name};
    if(pending.op==='Rename') { data.Path=pending.path+'/'+pending.selected.name; data.To=pending.path+'/'+name; }
    mutate(data); return true;
  },
  on_timer() {
    if(!running || paused) return true;
    request({Op:'CopyStatus'}).then(s=>{
      if(!running || paused) return;
      if(s.busy) message('Copy: '+s.bytes+' / '+s.total+' bytes');
      else if(s.error) message(s.error);
      else if(copyPending) { copyPending=false; refresh(); }
    }).catch(e=>message(e.message)); return true;
  },
  on_pause() { paused=true; ++generation; return true; },
  on_resume() { paused=false; refresh(); return true; },
  on_stop() {
    running=false; ++generation;
    for(const h of [timer,core,pocket,gui]) if(h!=null) brookesia.stop_service(h);
    timer=core=pocket=gui=null; return true;
  }
};
