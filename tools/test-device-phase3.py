"""Short real-board stage-3 assets/private-storage checks; one UART owner.

Creates only user.stage3-test and user.stage3-other if both IDs are absent.
No build, flash, erase, reset, host UI simulation or external network access.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import struct
import sys
import time
import zipfile

import bpk
from device import Device, ProtocolError

ROOT = Path(__file__).resolve().parents[1]
APP_ID, OTHER_ID = 'user.stage3-test', 'user.stage3-other'
OWN_IDS = (APP_ID, OTHER_ID)


def require(value, message):
    if not value:
        raise RuntimeError(message)


def healthy(device):
    return not device.capture.panic and not device.rebooted and device.error is None


def installed_ids(device):
    value = device.request('LIST', timeout=8)
    return [] if value == '-' else sorted(value.split(','))


def stats(device):
    return {k: int(v) for k, v in (item.split('=', 1) for item in device.request('DBG_RUNTIME').split(','))}


def wait_for(probe, predicate, message, timeout=12):
    deadline = time.monotonic() + timeout
    while True:
        value = probe()
        if predicate(value):
            return value
        require(time.monotonic() < deadline, message + ': ' + repr(value))
        time.sleep(.08)


def upload(device, data, expected=None):
    device.request('BEGIN', f'{len(data):08x}:{bpk.crc(data):08x}', timeout=8)
    try:
        for offset in range(0, len(data), 512):
            part = data[offset:offset+512]
            reply = device.request('DATA', f'{offset:08x}:{part.hex()}', timeout=8)
            require(int(reply) == offset + len(part), 'Incorrect acknowledged upload offset')
        result = device.request('END', timeout=30)
        if expected is not None:
            require(result == expected, 'Incorrect installed app ID')
        return result
    except BaseException:
        if healthy(device):
            try:
                device.request('ABORT', attempts=1)
            except Exception:
                pass
        raise


SCRIPT = r"""'use strict';
const version=__VERSION__, owner=__OWNER__, S=Ouse.storage;
let freezeArmed=false, busy=false;
function result(v){Ouse.ui.setText('result','result='+v);}
function stored(v){Ouse.ui.setText('stored','stored='+JSON.stringify(v));}
function assert(v,msg){if(!v)throw new Error(msg);}
async function denied(fn){try{await fn();return false;}catch(e){return true;}}
function action(id,fn){Ouse.ui.on(id,'click',()=>{
 if(busy)return;busy=true;result('busy:'+id);
 Promise.resolve().then(fn).then(v=>{result(v===undefined?id+':ok':v);busy=false;},e=>{result('FAIL:'+id+':'+String(e));busy=false;});
});}
action('assets',()=>{
 const list=Ouse.assets.list();assert(list.length===4,'asset count');
 assert(list.map(x=>x.id).sort().join(',')==='Alt,Cover,Labels,Raw','asset IDs');
 const cover=list.find(x=>x.id==='Cover');assert(cover.type==='image'&&cover.width===16&&cover.height===16&&cover.bytes===1024,'image metadata');
 const labels=JSON.parse(Ouse.assets.readText('Labels'));assert(labels.title==='你好'&&labels.revision===version,'text JSON');
 const a=new Uint8Array(Ouse.assets.readBytes('Raw'));assert(a.length===4&&a[0]===0&&a[1]===255&&a[2]===17&&a[3]===128,'binary');
 a[0]=9;assert(new Uint8Array(Ouse.assets.readBytes('Raw'))[0]===0,'binary copy');
 let rejected=0;for(const fn of [()=>Ouse.assets.readText('Cover'),()=>Ouse.assets.readBytes('Cover'),()=>Ouse.assets.readText('Raw'),()=>Ouse.assets.readText('../bad')]){try{fn();}catch(e){++rejected;}}
 assert(rejected===4,'asset type/ID errors');return 'assets:ok:'+version;
});
action('swap',()=>{Ouse.ui.setImage('cover','Alt');return 'image:Alt';});
action('save',async()=>{const value={owner,text:'你好',items:[1,true,null,{ok:2}]};assert(await S.set('shared',value)===true,'set bool');stored(await S.get('shared'));return 'saved:'+owner;});
action('load',async()=>{stored(await S.get('shared'));return 'loaded';});
action('keys',async()=>{stored(await S.keys());return 'keys';});
action('info',async()=>{const v=await S.info();assert(v.quotaBytes===65536&&v.keyLimit===32&&v.valueLimit===8192,'limits');stored(v);return 'info';});
action('remove',async()=>{assert(await S.remove('shared')===true,'remove existing');assert(await S.remove('shared')===false,'remove absent');assert(await S.get('shared')===null,'missing null');return 'remove:ok';});
action('clear',async()=>{const n=await S.clear();assert(Number.isInteger(n),'clear count');assert((await S.keys()).length===0,'keys clear');return 'clear:ok';});
action('invalid',async()=>{
 let n=0;for(const key of ['../escape','a/b','a\\b','a:','x'.repeat(32),'','\u0000']){if(await denied(()=>S.set(key,1)))++n;}
 assert(n===7,'invalid keys');return 'invalid:7';
});
action('values',async()=>{
 let n=0;const cyclic={};cyclic.self=cyclic;
 for(const value of [undefined,()=>{},cyclic,1n]){if(await denied(()=>S.set('bad',value)))++n;}
 assert(n===4,'unserializable JSON values');
 let chain=Promise.resolve(0);for(let i=0;i<40;++i)chain=chain.then(v=>v+1);
 assert(await chain===40,'ordinary Promise chain across task slices');
 let nested;
 await S.set('outer',{toJSON(){nested=S.set('inner',{ok:1});return {ok:2};}});
 await nested;assert((await S.get('inner')).ok===1&&(await S.get('outer')).ok===2,'toJSON reentrant independent slots');
 await S.remove('inner');await S.remove('outer');
 const jobs=[];
 const rejected=denied(()=>S.set('outer',{toJSON(){for(let i=0;i<8;++i)jobs.push(S.set('inner'+i,i));return 0;}}));
 assert(await rejected,'reentrant pending limit');await Promise.all(jobs);
 for(let i=0;i<8;++i){assert(await S.get('inner'+i)===i,'nested accepted request');await S.remove('inner'+i);}
 for(const pair of [[NaN,null],[Infinity,null],[[undefined],[null]],[{x:undefined},{}]]){
  await S.set('normalized',pair[0]);assert(JSON.stringify(await S.get('normalized'))===JSON.stringify(pair[1]),'JSON stringify normalization');
 }
 await S.remove('normalized');
 assert(await denied(()=>S.set('big','q'.repeat(8191))),'oversized value');
 await S.set('limit','q'.repeat(8190));assert((await S.get('limit')).length===8190,'value boundary');await S.remove('limit');return 'values:ok';
});
action('quota',async()=>{
 await S.clear();for(let i=0;i<8;++i)await S.set('q'+i,'q'.repeat(8190));
 const info=await S.info();assert(info.usedBytes===65536,'quota exact');assert(await denied(()=>S.set('overflow',0)),'quota exceeded');
 assert((await S.get('q0')).length===8190,'quota reject preserved');await S.clear();return 'quota:ok';
});
action('count',async()=>{
 await S.clear();for(let i=0;i<32;++i)await S.set('k'+String(i).padStart(2,'0'),i);
 const keys=await S.keys();assert(keys.length===32&&keys[0]==='k00'&&keys[31]==='k31','keys sorted');
 assert(await denied(()=>S.set('overflow',0)),'key limit');assert((await S.info()).keyCount===32,'key reject preserved');await S.clear();return 'count:ok';
});
action('system',()=>{const v=Ouse.system.storage();assert(v.ready&&v.totalBytes>0&&v.usedBytes+v.freeBytes===v.totalBytes&&v.packageCount>=2&&v.privateBytes>=0,'system stats');stored({total:v.totalBytes,used:v.usedBytes,free:v.freeBytes,packages:v.packageBytes,data:v.privateBytes,count:v.packageCount});return 'system:ok';});
Ouse.ui.on('freeze','click',()=>{freezeArmed=true;result('freeze:armed');Ouse.home();});
Ouse.ui.on('pending','click',()=>{
 for(let i=0;i<8;++i)S.set('pending'+i,{owner,index:i}).then(()=>{},()=>{});
 result('pending:submitted');Ouse.home();
});
globalThis.app={onPause(){if(freezeArmed){freezeArmed=false;S.set('frozen',{owner}).then(()=>result('freeze:'+Ouse.lifecycle.get().state),e=>result('FAIL:freeze:'+String(e)));}}};
Ouse.ui.setText('ready','ready=0;source='+version);
S.get('shared').then(v=>{stored(v);result('ready');},e=>result('FAIL:startup:'+String(e)));
"""


def image(color):
    return b'OIC1' + struct.pack('<HH', 16, 16) + bytes(color) * 256


def build_fixtures(output):
    output.mkdir(parents=True)
    packages = {}
    buttons = [('assets','Assets'), ('swap','Swap'), ('save','Save'), ('load','Load'), ('keys','Keys'),
               ('info','Info'), ('remove','Remove'), ('clear','Clear'), ('invalid','Invalid'),
               ('values','Values'), ('quota','Quota'), ('count','Count'), ('system','System'),
               ('freeze','Freeze'), ('pending','Pending')]
    children = [dict(type='label',id='ready',x=8,y=4,width=280,height=20,fontSize=14,text='ready=starting'),
                dict(type='label',id='result',x=8,y=27,width=280,height=24,fontSize=14,text='result=starting'),
                dict(type='label',id='stored',x=8,y=55,width=280,height=64,fontSize=12,text='stored=null'),
                dict(type='image',id='cover',x=12,y=122,width=32,height=32,src='Cover')]
    for i,(node,title) in enumerate(buttons):
        children.append(dict(type='button',id=node,x=8+(i%3)*96,y=164+(i//3)*38,width=88,height=32,fontSize=14,text=title))
    page=dict(api=1,units='dp',root=dict(type='container',id='page',width='fill',height='fill',children=children))
    for name,app_id,version,owner in [('main',APP_ID,'initial','main'),('update',APP_ID,'updated','main'),('other',OTHER_ID,'other','other')]:
        source=output/name;source.mkdir();(source/'assets').mkdir()
        manifest=dict(runtime='ouse-core',api=1,id=app_id,name='Stage 3 '+name,icon='icon.bin',assets=[
            dict(id='Cover',type='image',path='assets/cover.bin'),dict(id='Alt',type='image',path='assets/alt.bin'),
            dict(id='Labels',type='text',path='assets/labels.json'),dict(id='Raw',type='binary',path='assets/raw.bin')])
        (source/'manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
        (source/'page.json').write_text(json.dumps(page),encoding='utf-8')
        (source/'app.js').write_text(SCRIPT.replace('__VERSION__',json.dumps(version)).replace('__OWNER__',json.dumps(owner)),encoding='utf-8')
        (source/'icon.bin').write_bytes(image((140,100,20,255)))
        (source/'assets/cover.bin').write_bytes(image((210,100,25,255) if version!='updated' else (20,130,240,255)))
        (source/'assets/alt.bin').write_bytes(image((60,190,50,255)))
        (source/'assets/labels.json').write_text(json.dumps(dict(title='你好',revision=version),ensure_ascii=False),encoding='utf-8')
        (source/'assets/raw.bin').write_bytes(bytes((0,255,17,128)))
        archive=output/(name+'.bpk');bpk.pack(source,archive);packages[name]=archive.read_bytes()
    return packages


class UI:
    def __init__(self,device):
        self.device=device
    def tree(self):
        value=self.device.tree()
        (self.device.output/'last-tree.json').write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
        return value
    def text(self,prefix,missing_ok=False):
        texts=[n.get('text','') for n in self.tree()['nodes'] if n.get('text','').startswith(prefix)]
        if not texts and missing_ok:return None
        require(len(texts)==1,'Missing/ambiguous fixture status '+repr((prefix,texts)))
        return texts[0]
    def wait(self,prefix,expected,timeout=20):
        def probe():
            value=self.text(prefix,missing_ok=True)
            require(value is None or not value.startswith('result=FAIL:'),'Fixture operation failed: '+str(value))
            return value
        return wait_for(probe,lambda v:v==expected,'JS/UI result did not apply',timeout)
    def stored(self):
        return json.loads(self.text('stored=')[7:])
    def tap(self,title):
        nodes=[n for n in self.tree()['nodes'] if n.get('text')==title]
        require(nodes,'Fixture button missing: '+title)
        node=max(nodes,key=lambda n:n['depth']);a,b,c,d=node['bounds']
        require(c>a and d>b,'Fixture button clipped: '+title)
        self.device.tap((a+c)//2,(b+d)//2)
    def action(self,title,expected,timeout=20):
        self.tap(title);self.wait('result=','result='+expected,timeout)
    def open(self,app_id=APP_ID,source=None):
        self.device.request('OPEN',app_id,timeout=10)
        wait_for(self.device.state,lambda s:s['active']==app_id,'App did not open')
        wait_for(lambda:self.text('ready=',missing_ok=True),lambda v:bool(v) and v.startswith('ready=0;source=') and (source is None or v=='ready=0;source='+source),'App did not initialize')
        # First creation has no onResume callback. The top-level get Promise
        # supplies the actual startup-ready handshake.
        self.wait('result=','result=ready')
    def close(self,app_id=APP_ID):
        self.device.request('DBG_CLOSE',app_id,timeout=10)


def corrupt_packages(data):
    content=bpk._unzip(data)
    bad=bytearray(data);bad[60]^=1
    result={'crc':bytes(bad)}
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_STORED) as archive:
        for name,payload in content.items():archive.writestr(name,payload)
        archive.writestr('assets/undeclared.txt',b'unlisted')
    result['undeclared']=out.getvalue()
    return result


def run(device,args,report,save):
    hello=device.request('HELLO',timeout=8)
    require('storage=ready' in hello and 'debug=1' in hello,'Storage/debug unavailable')
    actual=device.request('DBG_BUILD');elf=Path(args.elf) if args.elf else ROOT/'build/ouse_core_desktop.elf'
    require(actual==hashlib.sha256(elf.read_bytes()).hexdigest(),'Running board and ELF differ')
    initial_ids=installed_ids(device)
    require(not set(OWN_IDS)&set(initial_ids),'Fixture ID exists; refusing overwrite')
    report.update(initial_ids=initial_ids,initial_state=device.state(),initial_runtime=stats(device),elf_sha256=actual,owns_test_ids=True)
    data=build_fixtures(device.output/'packages');save();ui=UI(device)
    def record(name,**details):
        require(healthy(device),'Board/UART failed')
        report['stages'].append(dict(stage=name,time=time.time(),runtime=stats(device),**details));save()
        print('Real-board stage 3:',name,'passed',flush=True)
    device.request('DBG_HOME')
    upload(device,data['main'],APP_ID);upload(device,data['other'],OTHER_ID)
    require(installed_ids(device)==sorted(initial_ids+list(OWN_IDS)),'Other packages changed')
    ui.open(source='initial');require(ui.stored() is None,'Fresh app private data is not empty')
    ui.action('Assets','assets:ok:initial');device.screenshot(device.output/'assets-initial.jpg')
    ui.action('Swap','image:Alt');device.screenshot(device.output/'assets-swapped.jpg')
    record('image_text_binary_assets_and_set_image')

    ui.action('Save','saved:main');expected=ui.stored();require(expected['owner']=='main' and expected['text']=='你好','JSON roundtrip failed')
    ui.close();ui.open(source='initial');require(ui.stored()==expected,'Close/reopen lost persisted JSON')
    record('json_roundtrip_context_close_reopen')
    ui.open(OTHER_ID,source='other');require(ui.stored() is None,'Other app could read main private data')
    ui.action('Save','saved:other');other=ui.stored();require(other['owner']=='other','Wrong private owner')
    ui.close(OTHER_ID)
    # Main still has its independent existing context, so OPEN does not run its
    # initial ready handshake again. Load is the explicit completion barrier.
    device.request('OPEN',APP_ID);ui.action('Load','loaded');require(ui.stored()==expected,'Same key crossed app isolation')
    record('per_application_key_isolation')

    for name,malformed in corrupt_packages(data['main']).items():
        try:upload(device,malformed)
        except ProtocolError:pass
        else:raise RuntimeError('Malformed package accepted: '+name)
        require(installed_ids(device)==sorted(initial_ids+list(OWN_IDS)),'Rejected package changed registry')
        ui.action('Load','loaded');require(ui.stored()==expected,'Rejected package changed private data')
    record('malformed_crc_undeclared_assets_preserve_app')
    upload(device,data['update'],APP_ID)
    require(device.state()['active']!=APP_ID,'Package replacement retained old foreground UI')
    ui.open(source='updated');require(ui.stored()==expected,'Update discarded private JSON')
    ui.action('Assets','assets:ok:updated');device.screenshot(device.output/'assets-updated.jpg')
    record('package_update_replaces_owned_assets_preserves_data')

    ui.action('Invalid','invalid:7');ui.action('Values','values:ok')
    ui.action('Load','loaded');require(ui.stored()==expected,'Rejected key/value corrupted existing JSON')
    ui.action('Info','info');info=ui.stored();require(info['keyCount']==1 and info['usedBytes']>0,'Private stats inaccurate')
    record('invalid_keys_json_values_and_value_boundary',info=info)
    ui.action('Quota','quota:ok',timeout=40);ui.action('Count','count:ok',timeout=40)
    ui.action('Info','info');require(ui.stored()['keyCount']==0,'Quota test left keys')
    record('byte_quota_and_key_limit')
    ui.action('Save','saved:main');ui.action('System','system:ok');system=ui.stored()
    record('cached_system_storage_usage',storage=system)

    ui.tap('Freeze');wait_for(device.state,lambda s:s['active']!=APP_ID,'Freeze did not navigate home')
    frozen=wait_for(lambda:stats(device),lambda v:v['runnable']==0,'Suspended application kept JS runnable')
    time.sleep(1)
    device.request('OPEN',APP_ID);ui.wait('result=','result=freeze:foreground')
    record('pending_promise_freeze_and_resume',frozen_runtime=frozen)
    ui.tap('Pending');wait_for(device.state,lambda s:s['active']!=APP_ID,'Pending test did not navigate home')
    ui.close();wait_for(lambda:stats(device),lambda v:v['contexts']==report['initial_runtime']['contexts'],'Closed Promise context leaked')
    ui.open(source='updated');ui.action('Keys','keys');pending_keys=ui.stored()
    require(all('pending'+str(i) in pending_keys for i in range(8)),'Accepted native writes lost during context close')
    ui.action('Remove','remove:ok');ui.action('Clear','clear:ok')
    record('close_with_pending_native_writes_and_clear')

    ui.action('Save','saved:main')
    ui.tap('Pending');wait_for(device.state,lambda s:s['active']!=APP_ID,'Pending uninstall did not navigate home')
    device.request('REMOVE',APP_ID,timeout=20)
    upload(device,data['main'],APP_ID);ui.open(source='initial')
    require(ui.stored() is None,'Uninstall/reinstall retained private data')
    ui.action('Info','info');require(ui.stored()['keyCount']==0,'Uninstall did not purge all keys')
    record('uninstall_fifo_purges_pending_writes_reinstall_empty')
    for app_id in OWN_IDS:device.request('REMOVE',app_id,timeout=20)
    final=wait_for(lambda:stats(device),lambda v:v['contexts']==report['initial_runtime']['contexts'] and v['timers']==report['initial_runtime']['timers'] and v['storage_jobs']==0 and v['storage_pending']==0,'Fixture contexts/timers/native requests leaked')
    require(installed_ids(device)==initial_ids,'Cleanup changed other packages')
    require(device.request('DBG_CHECK',timeout=20,attempts=1)=='ok=1','Heap integrity failed')
    record('cleanup_and_heap_integrity',heap=device.request('DBG_HEAP'),final_runtime=final)
    report.update(cleanup_completed=True,final_runtime=final);save()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM10');parser.add_argument('--elf');parser.add_argument('--output')
    args=parser.parse_args();report=dict(started=time.time(),status='running',stages=[])
    device=None;exit_code=0
    def save():
        if device is not None:(device.output/'phase3-result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    try:
        device=Device(args.port,args.output,args.elf);save();run(device,args,report,save);report['status']='passed'
    except (Exception,KeyboardInterrupt) as error:
        report.update(status='interrupted' if isinstance(error,KeyboardInterrupt) else 'failed',error=dict(type=type(error).__name__,message=str(error)))
        print('Stage 3 real board:',report['status'],error,file=sys.stderr,flush=True)
        if device is not None and healthy(device):
            try:
                report.update(failure_state=device.state(),failure_runtime=stats(device))
                (device.output/'failure-tree.json').write_text(json.dumps(device.tree(),ensure_ascii=False,indent=2),encoding='utf-8')
            except Exception as diagnostic_error:report['failure_diagnostic_error']=str(diagnostic_error)
        exit_code=130 if isinstance(error,KeyboardInterrupt) else 1
    finally:
        if device is not None:
            if report.get('owns_test_ids') and healthy(device):
                try:
                    for app_id in OWN_IDS:
                        if app_id in installed_ids(device):device.request('REMOVE',app_id,timeout=20)
                    require(installed_ids(device)==report['initial_ids'],'Final cleanup changed other packages')
                    original=report['initial_state']['active']
                    device.request('DBG_OPEN',original,timeout=10) if original else device.request('DBG_HOME')
                    report['cleanup_completed']=True
                except Exception as error:report.update(cleanup_error=str(error),status='failed');exit_code=1
            save()
            try:device.close()
            except Exception as error:report.update(capture_close_error=str(error),status='failed');exit_code=1
            report.update(panic_seen=bool(device.capture.panic),unexpected_reboot=device.rebooted,finished=time.time())
            if report['panic_seen'] or report['unexpected_reboot']:report['status'],exit_code='failed',1
            save();print('Evidence:',device.output.resolve(),flush=True)
    return exit_code


if __name__=='__main__':
    raise SystemExit(main())
