'use strict';
const N=Ouse.navigation;
let current='', epoch=0, results={}, pending={}, counts={theme:0,locale:0,display:0};
let brightnessTimer, brightnessDraft, audioOwned=false, backgroundTicks=0, noticeClicks=0, lastNotice='';
let selectedSSID='', passwordDraft='', imageGreen=false, storageBusy=false, privateOutput='', privateInfo=null;
let keyDraft='demo', valueDraft='{"message":"hello","count":1}', widgetCount=0, widgetValue=25;
let widgetSwitch=false, widgetCheck=false, widgetSelect=0, targetEnabled=true, stylePatched=false;
let inputDrafts={}, inputEvents='';
let textCache=Object.create(null);
let lastTaskId=0,lastTaskEvent='';
const wifiRows=[];
const wifiBound=[],wifiRowData=[];
let wifiListTimer,wifiGeneration=0,wifiNetworks=null;
const timeoutValues=[0,15,30,60,120,300];
function T(zh,en){return Ouse.locale.get()==='en'?en:zh;}
function text(value){
 const source=String(value).replace(/\u0000/g,'\\0');let out='',bytes=0;
 for(let i=0;i<source.length;){const cp=source.codePointAt(i),step=cp>65535?2:1,n=cp<128?1:cp<2048?2:cp<65536?3:4;
  if(bytes+n>240)break;out+=source.slice(i,i+step);bytes+=n;i+=step;}
 return out;
}
function put(id,value){const source=String(value);if(textCache[id]===source)return;Ouse.ui.setText(id,text(source));textCache[id]=source;}
function yes(value){return value?T('是','Yes'):T('否','No');}
function size(value){return (value/1024).toFixed(1)+' KiB';}
function now(){return Ouse.system.info().uptimeMs;}
function visible(page=current,token=epoch){const nav=N.get();return current===page&&epoch===token&&!nav.pending&&nav.page===page&&Ouse.lifecycle.get().visible;}
function result(page,zh,en,detail='',paint=true){results[page]={zh,en,detail:String(detail)};if(paint&&visible(page))drawResult();}
function error(page,value,token=epoch,paint=true){results[page]={zh:'错误',en:'Error',detail:String(value&&value.code||value&&value.message||value)};if(paint&&visible(page,token))drawResult();}
function drawResult(){const r=results[current];put('result',r?T(r.zh,r.en)+(r.detail?' · '+r.detail:''):'');}
function bind(id,event,fn){const page=current,token=epoch;Ouse.ui.on(id,event,value=>{if(!visible(page,token))return;if(event==='change')delete textCache[id];try{fn(value);}catch(e){error(page,e);}});}
function cancelWifiRows(){wifiGeneration++;if(wifiListTimer!==undefined)Ouse.timers.clear(wifiListTimer);wifiListTimer=undefined;}
function queueWifiRows(networks){
 const next=networks.slice(0,12);let same=wifiNetworks&&wifiNetworks.length===next.length;
 if(same)for(let i=0;i<next.length;i++){const a=next[i],b=wifiNetworks[i];if(a.ssid!==b.ssid||a.rssi!==b.rssi||a.secured!==b.secured){same=false;break;}}
 if(same)return;
 cancelWifiRows();wifiNetworks=next;const token=epoch,generation=wifiGeneration;let row=0;
 const step=()=>{
  if(generation!==wifiGeneration||!visible('wifi',token))return;
  wifiListTimer=undefined;const end=Math.min(row+2,12);
  for(;row<end;row++){const i=row,id='ap'+i,ap=next[i],shown=Boolean(ap);
   if(ap&&!wifiBound[i]){bind(id,'click',()=>{const selected=wifiRowData[i];if(selected){selectedSSID=selected.ssid;put('ssid',selectedSSID);result('wifi','已选择网络','Selected network',selectedSSID);}});wifiBound[i]=true;}
   wifiRowData[i]=ap||null;if(ap)put(id,ap.ssid+' · '+ap.rssi+' dBm'+(ap.secured?' *':''));
   if(wifiRows[i]!==shown){if(shown)Ouse.ui.show(id);else Ouse.ui.hide(id);wifiRows[i]=shown;}
  }
  if(row<12)wifiListTimer=Ouse.timers.setTimeout(step,100);
 };
 wifiListTimer=Ouse.timers.setTimeout(step,100);
}
function go(page){if(!N.push(page))result(current,'页面切换未接受','Navigation not accepted');}
function back(){const nav=N.get();if(nav.pending)return;if(nav.canGoBack){if(!N.back())result(current,'正在切换页面','Navigating');}else Ouse.home();}
function changed(page,ok,test,label){
 if(!ok){result(page,'请求未接受','Request rejected',label);return;}
 pending[page]={page,test,label,start:now()};result(page,'处理中','Working',label);settle(page);
}
function settle(page,paint=true){const job=pending[page];if(!job)return;try{
 const outcome=job.test();
 if(outcome===true){delete pending[page];result(page,job.completed?'操作完成':'当前值已更新',job.completed?'Completed':'Current value updated',job.label,paint);}
 else if(typeof outcome==='string'){delete pending[page];error(page,outcome,epoch,paint);}
 else if(now()-job.start>30000){delete pending[page];result(page,'等待超时','Timed out',job.label,paint);}
 }catch(e){delete pending[page];error(page,e,epoch,paint);}}
function requestDevice(group,label,invoke,page=current){
 const id=invoke();if(!id){result(page,'请求未接受','Request rejected',label);return;}
 lastTaskId=id;
 pending[page]={page,label,completed:true,start:now(),test(){const task=Ouse.tasks.get(id);
  if(!task)return T('任务记录已回收','Task record reclaimed');
  if(task.state==='succeeded')return true;
  if(task.state==='failed'||task.state==='cancelled')return task.state+' '+task.error;
  return false;}};
 result(page,'处理中','Working',label);settle(page);
}
function flushBrightness(){
 if(brightnessTimer!==undefined)Ouse.timers.clear(brightnessTimer);brightnessTimer=undefined;
 if(brightnessDraft===undefined)return;
 const value=brightnessDraft;brightnessDraft=undefined;
 changed('display',Ouse.display.setBrightness(value,true),()=>Ouse.display.get().brightness===value,'Brightness '+value+'%');
}
function stopOwned(page){if(audioOwned){audioOwned=false;requestDevice('audio',T('停止音频','Stop audio'),()=>Ouse.audio.stop(),page||'audio');}}
function cleanupTests(){
 Ouse.background.end();Ouse.notifications.cancel('api-demo');Ouse.notifications.cancel('api-ongoing');
 if(audioOwned)stopOwned('messages');else result('messages','测试已结束','Tests stopped');
}
function refresh(){if(!visible())return;
 if(current==='display'){
  const d=Ouse.display.get(),th=Ouse.theme.get();
  put('state',d.widthPx+' × '+d.heightPx+' px / '+d.contentWidthDp+' × '+d.contentHeightDp+' dp\n'+d.densityDpi+' dpi · '+d.fontPercent+'% · '+d.brightness+'%\n'+th.mode+' · '+Ouse.locale.get()+'\n'+th.colors.background+' / '+th.colors.text);
  put('events',T('变更次数','Change events')+' · '+counts.theme+' / '+counts.locale+' / '+counts.display);
  Ouse.ui.setValue('brightness',brightnessDraft===undefined?d.brightness:brightnessDraft);
 }else if(current==='audio'){
  const a=Ouse.audio.get();
  put('state',T('设备','Device')+' '+yes(a.available)+' · '+a.state+'\n'+T('音量','Volume')+' '+a.volume+'% · '+T('静音','Muted')+' '+yes(a.muted)+'\n'+a.positionMs+' / '+a.recordedMs+' ms\n'+T('错误','Error')+' '+a.error+' · '+a.completedId+'/'+a.requestId);
  put('level',T('麦克风电平','Microphone level')+' '+a.peak+'%');Ouse.ui.setValue('volume',a.volume);Ouse.ui.setValue('peak',a.peak);
 }else if(current==='wifi'){
  const w=Ouse.wifi.get();let mode=!w.enabled?T('已关闭','Off'):w.connecting?T('连接中','Connecting'):w.scanning?T('搜索中','Scanning'):w.connected?T('已连接','Connected'):w.ready?T('就绪','Ready'):T('初始化中','Initializing');
  put('state',mode+' · '+w.networks.length+' AP\n'+w.ssid+' '+w.ip+'\n'+w.rssi+' dBm · '+T('错误','Error')+' '+w.error+' / '+w.reason+'\n'+w.completedId+'/'+w.requestId);
  queueWifiRows(w.networks);
 }else if(current==='power'){
  const p=Ouse.power.get(),s=Ouse.system.info();
  put('state',T('电量','Battery')+' '+(p.percent<0?'--':p.percent+'%')+' / '+p.millivolts+' mV\n'+T('外部供电','External power')+' '+yes(p.external)+' · '+T('充电','Charging')+' '+yes(p.charging)+'\n'+T('睡眠','Asleep')+' '+yes(p.asleep)+' · '+T('错误','Error')+' '+p.error);
  put('system',s.coreVersion+' / '+s.idfVersion+'\nRAM '+size(s.internalFree)+' / '+size(s.internalTotal)+'\nPSRAM '+size(s.psramFree)+'\nFlash '+size(s.flashBytes)+' · CPU '+s.cpuMHz+' MHz\n'+(s.uptimeMs/1000).toFixed(1)+' s · Reset '+s.resetReason);
  Ouse.ui.setValue('sleepAfter',Math.max(0,timeoutValues.indexOf(p.sleepSeconds)));
 }else if(current==='messages'){
  const l=Ouse.lifecycle.get();
  put('state',l.state+' · '+T('后台','Background')+' '+yes(Ouse.background.isActive())+'\n'+T('后台计数','Background ticks')+' '+backgroundTicks+' · '+T('通知点击','Notice opens')+' '+noticeClicks+'\n'+lastNotice);
  const d=Ouse.system.diagnostics(),b=Ouse.background.get(),task=lastTaskId?Ouse.tasks.get(lastTaskId):null;
  put('taskState','JS '+(b.runnable?'run':'pause')+' / '+b.reason+'\nTasks '+d.nativeTasksActive+'/'+d.taskActiveLimit+' · JS '+size(d.jsHeapBytes)+'\n'+(task?'#'+task.id+' '+task.state+' '+task.cancelReason:lastTaskEvent)+'\n'+d.budgetClock);
  put('noticeList',Ouse.notifications.list().map(n=>n.id+' · '+n.title+(n.ongoing?' *':'')).join('\n'));
 }else if(current==='resources'){
  put('state',Ouse.assets.list().map(a=>a.id+' · '+a.type+' · '+a.bytes+' B').join('\n'));
 }else if(current==='storage'){
  const disk=Ouse.system.storage();
  put('state',T('文件系统','Filesystem')+' '+size(disk.usedBytes)+' / '+size(disk.totalBytes)+'\n'+T('应用数据','Private data')+' '+size(disk.privateBytes)+(privateInfo?'\n'+privateInfo.keyCount+'/32 · '+privateInfo.usedBytes+'/65536 B':''));
  put('output',privateOutput);
 }else if(current==='widgets'){
  put('state',T('点击','Taps')+' '+widgetCount+' · '+widgetValue+'%\n'+yes(widgetSwitch)+' / '+yes(widgetCheck)+' / '+(widgetSelect+1));
  put('sampleText',T('卡片与按钮','Card and button'));put('checkbox',Ouse.locale.t('checkbox'));
  Ouse.ui.setValue('switch',widgetSwitch);Ouse.ui.setValue('checkbox',widgetCheck);Ouse.ui.setValue('dropdown',widgetSelect);
  Ouse.ui.setValue('slider',widgetValue);Ouse.ui.setValue('progress',widgetValue);Ouse.ui.setValue('arc',widgetValue);
 }else if(current==='styles'){
  put('state',T('目标按钮','Target')+' · '+yes(targetEnabled)+'\n'+T('增量样式','Patched style')+' · '+yes(stylePatched));
 }else if(current.indexOf('input')===0){
  const d=Ouse.display.get();put('state',current.slice(5)+' · '+yes(d.keyboardVisible)+'\n'+d.keyboardHeightDp+' / '+d.keyboardInsetDp+' dp');put('inputEvents',inputEvents);
 }
 drawResult();
}
function storageAction(task){
 if(storageBusy){result('storage','处理中','Working');return;}
 storageBusy=true;const token=epoch,page=current;result(page,'处理中','Working');
 Promise.resolve().then(task).then(value=>{
  privateOutput=JSON.stringify(value);storageBusy=false;
  results.storage={zh:'操作成功',en:'Completed',detail:''};if(visible(page,token))refresh();
 },e=>{storageBusy=false;results.storage={zh:'错误',en:'Error',detail:String(e&&e.code||e&&e.message||e)};if(visible(page,token))refresh();});
}
let multiPoints={},multiPeak=0;
function clearMulti(){multiPoints={};multiPeak=0;}
function multiPaint(){
 const ids=Object.keys(multiPoints);multiPeak=Math.max(multiPeak,ids.length);
 Ouse.ui.setText('multiCount',ids.length+' / 2 · max '+multiPeak);
 for(let i=0;i<2;i++){const entries=ids.filter(id=>multiPoints[id].pad===i);Ouse.ui.setText('pad'+i,entries.length?entries.map(id=>'ID '+id+'\n'+multiPoints[id].x+', '+multiPoints[id].y).join('\n'):i?'右区 / Right':'左区 / Left');}
}
function multiLayout(){if(current!=='multi')return;const d=Ouse.display.get(),w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),pad=Math.floor((w-24)/2);Ouse.ui.layout([{id:'pad0',x:8,y:48,width:pad,height:h-56},{id:'pad1',x:16+pad,y:48,width:pad,height:h-56}]);}
function bindPage(){
 if(current!=='home')bind('back','click',back);
 if(current==='home'){
  for(const page of ['display','audio','wifi','power','controls','messages','resources'])bind('go_'+page,'click',()=>go(page));
 }else if(current==='multi'){
  clearMulti();multiLayout();multiPaint();for(let i=0;i<2;i++)for(const e of ['pointerdown','pointermove','pointerup','pointercancel'])bind('pad'+i,e,p=>{if(e==='pointerup'||e==='pointercancel')delete multiPoints[p.pointerId];else multiPoints[p.pointerId]={pad:i,x:p.x,y:p.y};multiPaint();});
 }else if(current==='display'){
  bind('theme','click',()=>{const v=Ouse.theme.get().mode==='light'?'dark':'light';changed('display',Ouse.theme.set(v),()=>Ouse.theme.get().mode===v,'Theme '+v);});
  bind('language','click',()=>{const v=Ouse.locale.get()==='en'?'zh_CN':'en';changed('display',Ouse.locale.set(v),()=>Ouse.locale.get()===v,'Locale '+v);});
  bind('brightness','change',value=>{brightnessDraft=value;
   if(!Ouse.display.setBrightness(value,false)){error('display',T('亮度请求未接受','Brightness rejected'));return;}
   if(brightnessTimer!==undefined)Ouse.timers.clear(brightnessTimer);
   brightnessTimer=Ouse.timers.setTimeout(flushBrightness,300);result('display','调节中','Adjusting',value+'%');});
  bind('refresh','click',refresh);
 }else if(current==='audio'){
  bind('volume','change',value=>changed('audio',Ouse.audio.setVolume(value),()=>Ouse.audio.get().volume===value,'Volume '+value+'%'));
  bind('mute','click',()=>{const v=!Ouse.audio.get().muted;changed('audio',Ouse.audio.setMuted(v),()=>Ouse.audio.get().muted===v,T('静音','Mute')+' '+yes(v));});
  for(const action of ['tone','record','replay'])bind(action,'click',()=>{
   const fn=()=>action==='tone'?Ouse.audio.testTone():action==='record'?Ouse.audio.record(3):Ouse.audio.playRecording();
   requestDevice('audio',Ouse.locale.t(action),()=>{const ok=fn();if(ok)audioOwned=true;return ok;});});
  bind('stop','click',()=>{audioOwned=false;requestDevice('audio',Ouse.locale.t('stop'),()=>Ouse.audio.stop());});bind('refresh','click',refresh);
 }else if(current==='wifi'){
  put('ssid',selectedSSID);put('password',passwordDraft);
  bind('ssid','change',v=>{selectedSSID=v;});bind('password','change',v=>{passwordDraft=v;});
  bind('enable','click',()=>requestDevice('wifi',Ouse.locale.t('enable'),()=>Ouse.wifi.setEnabled(!Ouse.wifi.get().enabled)));
  bind('scan','click',()=>requestDevice('wifi',Ouse.locale.t('scan'),()=>Ouse.wifi.scan()));
  bind('connect','click',()=>requestDevice('wifi',Ouse.locale.t('connect'),()=>Ouse.wifi.connect(selectedSSID,passwordDraft)));
  bind('disconnect','click',()=>requestDevice('wifi',Ouse.locale.t('disconnect'),()=>Ouse.wifi.disconnect()));
  bind('forget','click',()=>requestDevice('wifi',Ouse.locale.t('forget'),()=>Ouse.wifi.forget()));
 }else if(current==='power'){
  bind('sleepAfter','change',i=>{const v=timeoutValues[i];changed('power',Ouse.power.setTimeout(v),()=>Ouse.power.get().sleepSeconds===v,v+' s');});
  bind('sleep','click',()=>changed('power',Ouse.power.sleep(),()=>Ouse.power.get().asleep===true,T('熄屏','Screen off')));
  bind('wake','click',()=>changed('power',Ouse.power.wake(),()=>Ouse.power.get().asleep===false,T('唤醒','Wake')));bind('refresh','click',refresh);
 }else if(current==='controls'){
  bind('go_multi','click',()=>go('multi'));
  for(const page of ['widgets','styles','inputResize','inputPan','inputOverlay'])bind('go_'+page,'click',()=>go(page));
 }else if(current==='messages'){
  bind('publish','click',()=>{const ok=Ouse.notifications.publish({id:'api-demo',title:T('接口测试','API Test'),body:T('点击返回通知测试页','Tap to open notification tests'),payload:'messages'});result('messages',ok?'通知已发送':'发送失败',ok?'Published':'Publish failed');refresh();});
  bind('update','click',()=>{const ok=Ouse.notifications.update({id:'api-demo',title:T('通知已更新','Updated notice'),body:T('来自接口测试','From API Test'),payload:'messages:updated'});result('messages',ok?'通知已更新':'更新失败',ok?'Updated':'Update failed');refresh();});
  bind('ongoing','click',()=>{const ok=Ouse.notifications.publish({id:'api-ongoing',title:T('接口测试进行中','API test running'),body:T('由应用主动取消','Canceled by the app'),payload:'messages:ongoing',ongoing:true});result('messages',ok?'常驻通知已发送':'发送失败',ok?'Ongoing notice published':'Publish failed');refresh();});
  bind('cancel','click',()=>{const a=Ouse.notifications.cancel('api-demo'),b=Ouse.notifications.cancel('api-ongoing');result('messages','已取消','Canceled',String(Number(a)+Number(b)));refresh();});
  bind('taskProbe','click',()=>{const promise=Ouse.storage.info();lastTaskId=promise.taskId||0;promise.then(()=>{if(current==='messages')refresh();},e=>error('messages',e));refresh();});
  bind('taskCancel','click',()=>{result('messages','取消请求','Cancel request',lastTaskId&&Ouse.tasks.cancel(lastTaskId)?'accepted':'unavailable');refresh();});
  bind('backgroundOn','click',()=>{const ok=Ouse.background.begin('api-test');result('messages',ok?'后台已开启':'后台未开启',ok?'Background enabled':'Background rejected');refresh();});
  bind('backgroundOff','click',()=>{Ouse.background.end();result('messages','后台已停止','Background stopped');refresh();});
  bind('backgroundHome','click',()=>Ouse.home());bind('cleanup','click',()=>{cleanupTests();refresh();});
 }else if(current==='resources'){
  Ouse.ui.setImage('picture',imageGreen?'green':'blue');
  bind('list','click',()=>result('resources','资源数量','Asset count',Ouse.assets.list().length));
  bind('swap','click',()=>{imageGreen=!imageGreen;Ouse.ui.setImage('picture',imageGreen?'green':'blue');result('resources','图片已切换','Image changed',imageGreen?'green':'blue');});
  bind('readText','click',()=>{const words=JSON.parse(Ouse.assets.readText('words'));result('resources','文本读取成功','Text read',words[Ouse.locale.get()].hello);});
  bind('readBytes','click',()=>{const bytes=new Uint8Array(Ouse.assets.readBytes('raw'));result('resources','二进制读取成功','Bytes read',Array.from(bytes).join(', '));});
  bind('go_storage','click',()=>go('storage'));
  bind('invalid','click',()=>{let n=0;for(const fn of [()=>Ouse.assets.readText('blue'),()=>Ouse.assets.readBytes('../escape')]){try{fn();}catch(e){n++;}}result('resources',n===2?'非法资源访问已拒绝':'测试失败',n===2?'Invalid asset access rejected':'Test failed',n+'/2');});
 }else if(current==='storage'){
  put('key',keyDraft);put('value',valueDraft);
  const infoEpoch=epoch;Ouse.storage.info().then(info=>{privateInfo=info;if(visible('storage',infoEpoch))refresh();},e=>error('storage',e,infoEpoch));
  bind('key','change',v=>{keyDraft=v;});bind('value','change',v=>{valueDraft=v;});
  bind('get','click',()=>{const key=keyDraft;storageAction(()=>Ouse.storage.get(key));});
  bind('set','click',()=>{let value;try{value=JSON.parse(valueDraft);}catch(e){error('storage',T('JSON 格式错误','Invalid JSON'));return;}const key=keyDraft;storageAction(async()=>{await Ouse.storage.set(key,value);privateInfo=await Ouse.storage.info();return await Ouse.storage.get(key);});});
  bind('remove','click',()=>{const key=keyDraft;storageAction(async()=>{const v=await Ouse.storage.remove(key);privateInfo=await Ouse.storage.info();return v;});});
  bind('keys','click',()=>storageAction(()=>Ouse.storage.keys()));
  bind('clear','click',()=>storageAction(async()=>{const n=await Ouse.storage.clear();privateInfo=await Ouse.storage.info();return {removed:n};}));
  bind('info','click',()=>storageAction(async()=>{privateInfo=await Ouse.storage.info();return privateInfo;}));
  bind('invalid','click',()=>storageAction(async()=>{let rejected=0;try{await Ouse.storage.set('../invalid',1);}catch(e){rejected++;}try{await Ouse.storage.set('oversized','x'.repeat(8191));}catch(e){rejected++;}if(rejected!==2)throw new Error('Invalid storage request accepted');return {rejected};}));
 }else if(current==='widgets'){
  bind('target','click',()=>{widgetCount++;result('widgets','点击事件','Click event',widgetCount);refresh();});
  bind('switch','change',v=>{widgetSwitch=v;result('widgets','开关事件','Switch event',String(v));refresh();});
  bind('checkbox','change',v=>{widgetCheck=v;result('widgets','复选框事件','Checkbox event',String(v));refresh();});
  bind('dropdown','change',v=>{widgetSelect=v;result('widgets','选择事件','Selection event',v+1);refresh();});
  bind('slider','change',v=>{widgetValue=v;refresh();});bind('arc','change',v=>{widgetValue=v;refresh();});
  bind('refresh','click',()=>{result('widgets','控件读取','Widget values',Ouse.ui.getText('sampleText')+' / '+Ouse.ui.getValue('slider')+' / '+Ouse.ui.isVisible('demoCard'));refresh();});
 }else if(current==='styles'){
  const apply=()=>{
   Ouse.ui.setEnabled('target',targetEnabled);
   Ouse.ui.setStyle('target',{states:{pressed:{backgroundColor:'#805020'},disabled:{backgroundColor:'#323238',opacity:130}}});
   if(stylePatched){Ouse.ui.setStyle('styleCard',{backgroundColor:'#244A68',borderColor:'#F2C04A',borderWidth:2});Ouse.ui.setStyle('styleCard',{radius:28});}
   else Ouse.ui.setStyle('styleCard',{backgroundColor:null,borderColor:null,borderWidth:0,radius:20});
  };
  apply();bind('target','click',()=>result('styles','按钮点击','Target clicked'));
  bind('stylePatch','click',()=>{stylePatched=true;apply();result('styles','增量样式已应用','Style patched');refresh();});
  bind('styleReset','click',()=>{stylePatched=false;apply();result('styles','样式已恢复','Style reset');refresh();});
  bind('toggleEnabled','click',()=>{targetEnabled=!targetEnabled;apply();refresh();});
  for(const id of ['dialog','sheet']){
   bind(id==='dialog'?'openDialog':'openSheet','click',()=>{const ok=Ouse.ui.show(id);result('styles',ok?'弹层已打开':'弹层未打开',ok?'Modal shown':'Modal not shown',id+' / '+Ouse.ui.isVisible(id));});
   bind(id+'Close','click',()=>{Ouse.ui.hide(id);result('styles','弹层已关闭','Modal hidden',id);});
   bind(id,'dismiss',reason=>result('styles','弹层已关闭','Modal dismissed',id+' / '+reason));
  }
 }else if(current.indexOf('input')===0){
  const page=current;if(!inputDrafts[page])inputDrafts[page]={text:'',number:'',password:''};
  for(const id of ['text','number','password']){
   put(id,inputDrafts[page][id]);
   bind(id,'change',v=>{inputDrafts[page][id]=v;inputEvents=id+' · change · '+v.length;result(page,'输入变更','Input changed',id+' / '+v.length);refresh();});
   for(const event of ['focus','blur','submit'])bind(id,event,()=>{inputEvents=id+' · '+event;result(page,'输入事件','Input event',id+' / '+event);refresh();});
  }
  bind('focus','click',()=>Ouse.ui.focus('text'));bind('blur','click',()=>{for(const id of ['text','number','password'])Ouse.ui.blur(id);});
 }
}
Ouse.navigation.onChange(event=>{
 if(event.previous==='display'&&brightnessDraft!==undefined)flushBrightness();
 if(event.previous==='audio'&&event.page!=='audio')stopOwned('audio');
 cancelWifiRows();wifiNetworks=null;wifiBound.length=0;wifiRowData.length=0;
 current=event.page;epoch++;textCache=Object.create(null);wifiRows.length=0;bindPage();refresh();
});
Ouse.theme.onChange(()=>{counts.theme++;settle('display',false);refresh();});
Ouse.locale.onChange(()=>{counts.locale++;settle('display',false);refresh();});
Ouse.display.onChange(()=>{if(current==='multi'){clearMulti();multiLayout();multiPaint();}counts.display++;settle('display',false);refresh();});
Ouse.power.onChange(()=>{settle('power',false);if(current==='power')refresh();});
Ouse.audio.onChange(()=>{settle('audio',false);settle('messages',false);if(current==='audio'||current==='messages')refresh();});
Ouse.wifi.onChange(()=>{settle('wifi',false);if(current==='wifi')refresh();});
Ouse.tasks.onComplete(task=>{
 lastTaskEvent='#'+task.id+' '+task.state;settle('audio',false);settle('wifi',false);
 if(current==='messages'||current==='audio'||current==='wifi')refresh();
});
Ouse.notifications.onOpen(event=>{
 noticeClicks++;lastNotice=event.id+' · '+event.payload;
 result('messages','通知已打开','Notification opened',event.id);
 const nav=N.get();if(!nav.pending&&nav.page!=='messages')N.push('messages');else if(current==='messages')refresh();
});
Ouse.timers.setInterval(()=>{
 if(Ouse.lifecycle.get().state==='background')backgroundTicks++;
 for(const page of Object.keys(pending))settle(page);
 if(current==='audio'||current==='power'||current==='messages')refresh();
},500);
globalThis.app={
 onPause(){clearMulti();cancelWifiRows();wifiNetworks=null;if(brightnessDraft!==undefined)flushBrightness();stopOwned('audio');},
 onResume(){if(current==='multi')multiPaint();refresh();},
 onClose(){cancelWifiRows();Ouse.background.end();if(audioOwned)Ouse.audio.stop();}
};
