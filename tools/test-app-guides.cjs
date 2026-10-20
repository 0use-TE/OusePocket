const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
for(const [name,kind] of [['netease','consumerReady'],['mouse','mouseReady'],['presenter','keyboardReady'],['coder','keyboardReady']]){
 const text={},shown={},handlers={},timers=[],requested=[],settings=[],reports=[];
 const permissions={bluetooth:'ask',hid:'ask'},link={enabled:false,ready:false,connected:false,session:0};let opens=0,closes=0;
 const display={contentWidthDp:320,contentHeightDp:416};let displayChanged;
 const api={display:{get:()=>display,onChange(fn){displayChanged=fn;}},ui:{layout(frames){for(const f of frames)assert(f.x>=0&&f.y>=0&&f.x+f.width<=display.contentWidthDp&&f.y+f.height<=display.contentHeightDp,JSON.stringify(f));},setText:(id,value)=>text[id]=value,show:id=>shown[id]=true,hide:id=>shown[id]=false,on:(id,event,fn)=>handlers[id+':'+event]=fn,setImage(){}},
  permissions:{get:key=>permissions[key],request(key){requested.push(key);return permissions[key];}},system:{openSettings:key=>settings.push(key)},
  bluetooth:{get:()=>link,open(){link.session=++opens;return opens;},close(){closes++;link.session=0;},release(){},keyboard:(...r)=>reports.push(r),consumer:(...r)=>reports.push(r),mouse:(...r)=>reports.push(r)},
  timers:{setInterval:fn=>timers.push(fn),setTimeout(){}}};
 const source=fs.readFileSync(`apps/${name}/app.js`,'utf8');assert(source.includes(fs.readFileSync('tools/app-shared/bluetooth-guide.js','utf8').trim()));
 const context=vm.createContext({Ouse:api});vm.runInContext(source,context);const tick=()=>timers.forEach(fn=>fn()),click=()=>handlers['connect:click']();
 if(name==='netease'||name==='presenter')for(const [w,h] of [[320,416],[480,256],[480,240],[320,400]]){display.contentWidthDp=w;display.contentHeightDp=h;displayChanged();}
 assert.deepEqual(requested,['bluetooth']);for(let i=0;i<20;i++)tick();assert.equal(requested.length,1,'waiting must not repeatedly request');
 permissions.bluetooth='denied';for(let i=0;i<20;i++)tick();assert.equal(text.connect,'权限设置');assert.equal(requested.length,1);assert.equal(opens,0);click();assert.equal(settings.at(-1),'permissions');assert.equal(permissions.bluetooth,'denied');
 permissions.bluetooth='granted';tick();assert.equal(requested.at(-1),'hid');permissions.hid='granted';tick();assert.equal(text.connect,'开启蓝牙');click();assert.equal(settings.at(-1),'bluetooth');
 link.enabled=link.ready=true;tick();assert.equal(text.status,'尚未连接设备');assert.equal(opens,0);link.connected=true;tick();assert.equal(opens,1);assert.equal(text.status,'等待输入服务');assert.equal(shown.connect,true);
 link[kind]=true;tick();assert.equal(text.status,'已连接');assert.equal(shown.connect,false);for(let i=0;i<5;i++)tick();assert.equal(opens,1);
 link.connected=false;tick();assert.equal(shown.connect,true);assert.equal(text.connect,'连接蓝牙');link.connected=true;link.session=0;tick();assert.equal(opens,2);
 context.app.onPause();assert.equal(closes,1);context.app.onResume();tick();permissions.hid='denied';tick();assert.equal(text.connect,'权限设置');assert.equal(shown.connect,true);context.app.onClose();
 console.log(`PASS ${name}: ask/deny guidance, settings actions, disabled/disconnected/service readiness, no repeated request/open, revoke and resume`);
}
