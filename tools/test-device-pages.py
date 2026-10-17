"""Verify page ownership, native Back and context persistence on the actual board.

Only the absent user.pages-test fixture is installed and removed. No reset,
settings changes, host rendering, or access to another application's data.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import struct
import time

from device import Device, ProtocolError
import bpk

ROOT = Path(__file__).resolve().parents[1]
APP_ID = 'user.pages-test'
spec = importlib.util.spec_from_file_location('phase3_helpers', ROOT/'tools/test-device-phase3.py')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
require, wait_for, upload = helpers.require, helpers.wait_for, helpers.upload
stats, installed_ids, healthy = helpers.stats, helpers.installed_ids, helpers.healthy

SCRIPT = r'''"use strict";
const stringSource='320中文😃', stringParts=[];
for(const ch of stringSource){stringParts.push(ch);if(stringParts.length>6)throw new Error('String iterator did not advance');}
if(stringParts.join('')!==stringSource||stringParts.length!==6)throw new Error('Unicode string iteration changed');
const stringIterator=stringSource[Symbol.iterator]();
for(let i=0;i<stringParts.length;++i){const part=stringIterator.next();if(part.done||part.value!==stringParts[i])throw new Error('String next() changed');}
if(!stringIterator.next().done)throw new Error('String next() did not finish');
let ticks=0, visits=0, complete=false, prototypeTrap=false;
const N=Ouse.navigation;
function status(v){Ouse.ui.setText('status','status='+v);}
function bind(change){
 if(prototypeTrap){delete Object.prototype.gap;prototypeTrap=false;}
 ++visits;
 Ouse.ui.setText('location','page='+change.page+';depth='+change.depth+';visits='+visits);
 Ouse.ui.setText('ticks','ticks='+ticks);
 status('ready:'+change.page);
 Ouse.ui.on('next','click',()=>{
   const n=N.get(), id=n.page==='home'?'p1':'p'+((Number(n.page.slice(1))+1)%16);
   const accepted=N.push(id==='p0'?'home':id);
   if(!accepted)status('push:false');
 });
 Ouse.ui.on('back','click',()=>{if(!N.back())status('back:false');});
 if(change.page==='home'||change.page==='p1'){
 Ouse.ui.on('replace','click',()=>{if(!N.replace('p2'))status('replace:false');});
 Ouse.ui.on('unknown','click',()=>{try{N.push('absent');status('unknown:accepted');}catch(e){status('unknown:rejected');}});
 Ouse.ui.on('prototype','click',()=>{prototypeTrap=true;Object.defineProperty(Object.prototype,'gap',{configurable:true,get(){throw new Error('Inherited JSON getter executed');}});N.push('p1');});
 Ouse.ui.on('write','click',()=>{
   complete=false;
   Ouse.storage.set('proof',{nonce:'pages-proof'}).then(()=>{complete=true;},()=>{complete='error';});
   N.push('p1');
 });
 Ouse.ui.on('read','click',()=>{
   Ouse.storage.get('proof').then(value=>status('storage:'+complete+':'+(value&&value.nonce)),e=>status('storage:error:'+String(e)));
 });
 Ouse.ui.on('input','submit',()=>status('submitted'));
 Ouse.ui.on('modalOpen','click',()=>Ouse.ui.show('modal'));
 Ouse.ui.on('modal','dismiss',reason=>status('modal:'+reason));
 }
}
N.onChange(bind);
Ouse.timers.setInterval(()=>{++ticks;const n=N.get();if(!n.pending)Ouse.ui.setText('ticks','ticks='+ticks);},100);
'''


def fixtures(output):
    source=output/'package-source';source.mkdir()
    (source/'manifest.json').write_text(json.dumps(dict(runtime='ouse-core',api=1,id=APP_ID,name='Pages test',icon='icon.bin')),encoding='utf-8')
    (source/'icon.bin').write_bytes(b'OIC1'+struct.pack('<HH',4,4)+bytes((40,150,220,255))*16)
    (source/'app.js').write_text(SCRIPT,encoding='utf-8')
    pages=[]
    for i in range(16):
        children=[dict(type='label',id='location',text='page=starting',width='fill',height=22,fontSize=14),
                  dict(type='label',id='status',text='status=starting',width='fill',height=22,fontSize=14),
                  dict(type='label',id='ticks',text='ticks=0',width='fill',height=20,fontSize=12)]
        buttons=[('next','Next'),('back','Back')]
        if i<2:buttons.extend([('replace','Replace'),('unknown','Unknown'),('prototype','Prototype'),('write','Write + next'),('read','Read'),('modalOpen','Modal')])
        children.append(dict(type='container',id='buttons',layout='column',width='fill',height='content',gap=2,children=[
            dict(type='button',id=key,text=text,width='fill',height=40,fontSize=14) for key,text in buttons]))
        if i<2:
            children.append(dict(type='textInput',id='input',width='fill',height=44,placeholder='Input',inputType='text',keyboardMode='resize'))
            children.append(dict(type='dialog',id='modal',text='Page modal',width=230,height=100,children=[dict(type='label',id='modalText',text='Close with Back',width='fill',height='content')]))
        # More than 128 nodes across the document, while every live page stays
        # comfortably below the per-page limit. Padding nodes are not visible.
        if i<2:children.extend(dict(type='label',id='extra'+str(n),text='extra',width=1,height=1,style=dict(hidden=True)) for n in range(8))
        pages.append(dict(id='home' if i==0 else 'p'+str(i),root=dict(type='container',id='page',layout='column',scroll=True,padding=12,gap=4,width='fill',height='fill',children=children)))
    doc=dict(api=1,units='dp',initialPage='home',pages=pages)
    (source/'page.json').write_text(json.dumps(doc,separators=(',',':')),encoding='utf-8')
    archive=output/'pages.bpk';bpk.pack(source,archive)
    # Invalid inactive pages must fail isolated native schema validation, not
    # wait until the user enters them. Build ZIPs directly to test the board.
    import io,zipfile,copy
    bad={}
    for label in ('duplicate','missing-initial','inactive-node','too-many-pages','root-and-pages'):
        value=copy.deepcopy(doc)
        if label=='duplicate':value['pages'][-1]['id']='home'
        elif label=='missing-initial':value['initialPage']='missing'
        elif label=='inactive-node':value['pages'][-1]['root']['children'][0]['type']='invalid-control'
        elif label=='too-many-pages':value['pages'].append(dict(id='overflow',root=value['pages'][0]['root']))
        else:value['root']=value['pages'][0]['root']
        buffer=io.BytesIO()
        with zipfile.ZipFile(buffer,'w',zipfile.ZIP_STORED) as z:
            for name in ('manifest.json','app.js','icon.bin'):z.writestr(name,(source/name).read_bytes())
            z.writestr('page.json',json.dumps(value,separators=(',',':')))
        bad[label]=buffer.getvalue()
    return archive.read_bytes(),bad


class UI:
    def __init__(self,device):self.device=device
    def tree(self):
        tree=self.device.tree()
        (self.device.output/'last-tree.json').write_text(json.dumps(tree,ensure_ascii=False,indent=2),encoding='utf-8')
        return tree
    def text(self,prefix):
        found=[n['text'] for n in self.tree()['nodes'] if n.get('text','').startswith(prefix)]
        require(len(found)<=1,'Ambiguous state: '+prefix)
        return found[0] if found else None
    def wait(self,prefix,expected):return wait_for(lambda:self.text(prefix),lambda v:v==expected,'Page status not applied',12)
    def page(self,page,depth):
        return wait_for(lambda:self.text('page='),lambda s:bool(s) and s.startswith('page='+page+';depth='+str(depth)+';'),'Page navigation did not settle',12)
    def tap(self,text):
        # Bring a control into view using only production pointer input.
        for direction in (False,True):
            for _ in range(5):
                tree=self.tree();stack=[];found=[]
                for node in tree['nodes']:
                    while stack and stack[-1]['depth']>=node['depth']:stack.pop()
                    if node.get('text')==text:
                        a,b,c,d=node['bounds']
                        for parent in stack:
                            x,y,z,w=parent['bounds'];a,b,c,d=max(a,x),max(b,y),min(c,z),min(d,w)
                        a,b,c,d=max(a,10),max(b,45),min(c,309),min(d,450)
                        if c-a>4 and d-b>10:found.append((node['depth'],(a,b,c,d)))
                    stack.append(node)
                if found:
                    a,b,c,d=max(found,key=lambda x:x[0])[1];self.device.tap((a+c)//2,(b+d)//2);return
                self.device.swipe(286,330 if direction else 170,286,170 if direction else 365,180,hold_ms=120)
                time.sleep(.15)
        raise RuntimeError('Visible control missing: '+text)
    def edge(self,left=True):
        self.device.swipe(3 if left else 316,240,95 if left else 224,240,180)


def run(device,args,report,save):
    require(device.request('DBG_BUILD')==hashlib.sha256(Path(args.elf).read_bytes()).hexdigest(),'ELF mismatch')
    original=installed_ids(device);require(APP_ID not in original,'Test package already exists')
    report.update(initial_ids=original,initial_state=device.state(),initial_runtime=stats(device),owns_fixture=True)
    data,bad=fixtures(device.output);ui=UI(device)
    def record(name,**details):
        require(healthy(device),'Board or transport failed')
        report['stages'].append(dict(name=name,time=time.time(),runtime=stats(device),**details));save();print('Pages:',name,'passed',flush=True)
    upload(device,data,APP_ID);device.request('OPEN',APP_ID);ui.page('home',1)
    initial=stats(device);require(initial['widgets']<96,'All page widgets were built simultaneously')
    record('sixteen_pages_validated_only_current_page_built',widgets=initial['widgets'])
    ui.tap('Unknown');ui.wait('status=','status=unknown:rejected');ui.page('home',1)
    ui.tap('Next');ui.page('p1',2);ui.edge(True);ui.page('home',1)
    ui.tap('Next');ui.page('p1',2);ui.edge(False);ui.page('home',1)
    record('unknown_target_and_both_native_back_edges')
    ui.tap('Prototype');ui.page('p1',2);ui.tap('Back');ui.page('home',1)
    record('static_page_models_ignore_application_prototype_getters')
    before=int(ui.text('ticks=')[6:]);ui.tap('Next');ui.page('p1',2)
    ui.tap('Replace');ui.page('p2',2);ui.tap('Back');ui.page('home',1)
    after=int(ui.text('ticks=')[6:]);require(after>before,'Timer or JS context reset during navigation')
    record('replace_preserves_stack_and_js_timer',tick_before=before,tick_after=after)
    ui.tap('Modal');wait_for(lambda:ui.text('Page modal'),lambda t:t=='Page modal','Modal not shown')
    ui.edge();ui.wait('status=','status=modal:back');ui.page('home',1)
    record('modal_back_precedes_page_navigation')
    ui.tap('Write + next');ui.page('p1',2);ui.tap('Read');ui.wait('status=','status=storage:true:pages-proof')
    device.request('DBG_HOME');require(device.state()['active']=='','Home failed')
    device.request('OPEN',APP_ID);ui.page('p1',2);ui.tap('Back');ui.page('home',1)
    record('pending_storage_context_and_stack_survive_home')
    for depth in range(2,9):ui.tap('Next');ui.page('p'+str(depth-1),depth)
    ui.tap('Next');ui.wait('status=','status=push:false');ui.page('p7',8)
    for depth in range(7,0,-1):ui.tap('Back');ui.page('home' if depth==1 else 'p'+str(depth-1),depth)
    record('bounded_stack_and_return_rebuild')
    for label,payload in bad.items():
        try:upload(device,payload)
        except ProtocolError:pass
        else:raise RuntimeError('Invalid page document accepted: '+label)
        require(installed_ids(device)==sorted(original+[APP_ID]),'Rejected update changed registry')
        ui.page('home',1)
    record('all_inactive_page_schema_and_stack_validation',rejected=list(bad))
    for _ in range(12):ui.tap('Next');ui.page('p1',2);ui.tap('Back');ui.page('home',1)
    final_live=stats(device);require(final_live['widgets']==initial['widgets'],'Page widgets accumulated across navigation')
    require(final_live['errors']==report['initial_runtime']['errors'],'Navigation generated a JS fault')
    require(device.request('DBG_CHECK',timeout=20,attempts=1)=='ok=1','Heap damaged')
    record('repeated_navigation_heap_and_current_page_widget_count',heap=device.request('DBG_HEAP'))
    ui.edge();wait_for(device.state,lambda s:s['active']=='','Root Back did not go home')
    device.request('DBG_CLOSE',APP_ID);wait_for(lambda:stats(device),lambda s:s['contexts']==report['initial_runtime']['contexts'],'Context retained after close')
    device.request('OPEN',APP_ID);ui.page('home',1);ui.tap('Read');ui.wait('status=','status=storage:false:pages-proof')
    record('root_back_and_close_reopen_private_data')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM10');parser.add_argument('--elf',default=str(ROOT/'build/ouse_core_desktop.elf'));parser.add_argument('--output',required=True)
    args=parser.parse_args();report=dict(started=time.time(),status='running',stages=[]);device=None;exit_code=0
    def save():
        if device:(device.output/'pages-result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    try:
        device=Device(args.port,args.output,args.elf);report['elf_sha256']=device.request('DBG_BUILD');run(device,args,report,save);report['status']='passed'
    except (Exception,KeyboardInterrupt) as error:
        report.update(status='failed',error=str(error));exit_code=1;print('Pages failed:',error,flush=True)
        if device and healthy(device):
            try:report['failure_state']=device.state();report['failure_runtime']=stats(device)
            except Exception:pass
    finally:
        if device:
            if report.get('owns_fixture') and healthy(device):
                try:
                    if APP_ID in installed_ids(device):device.request('REMOVE',APP_ID,timeout=30)
                    require(installed_ids(device)==report['initial_ids'],'Cleanup changed existing apps')
                    original=report['initial_state']['active'];device.request('DBG_OPEN',original) if original else device.request('DBG_HOME')
                    report.update(cleanup_completed=True,final_runtime=stats(device))
                except Exception as error:report.update(status='failed',cleanup_error=str(error));exit_code=1
            device.close();report.update(finished=time.time(),panic_seen=bool(device.capture.panic),unexpected_reboot=device.rebooted,serial_closed=not device.port.is_open)
            if report['panic_seen'] or report['unexpected_reboot']:report['status'],exit_code='failed',1
            save()
    return exit_code


if __name__=='__main__':raise SystemExit(main())
