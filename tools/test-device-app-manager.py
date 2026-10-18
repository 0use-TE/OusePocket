"""Non-destructive management UI checks: only disposable QA packages are changed."""
import argparse
import io
import json
from pathlib import Path
import struct
import time
import zipfile
import bpk

QA = 'user.manager-qa'
CONTROL = 'user.manager-control'


def package(app_id, name):
    manifest = {'runtime': 'ouse-core', 'api': 1, 'id': app_id, 'name': name, 'icon': 'icon.bin', 'capabilities': {'alarms': True}}
    children = [{'type': 'label', 'id': 'status', 'text': 'Loading', 'width': 'fill', 'height': 50},
                {'type': 'label', 'id': 'alarms', 'text': '', 'width': 'fill', 'height': 40},
                {'type': 'button', 'id': 'seed', 'text': 'Seed', 'width': 'fill', 'height': 50}]
    page = {'api': 1, 'units': 'dp', 'root': {'type': 'container', 'id': 'root', 'width': 'fill', 'height': 'fill', 'layout': 'column', 'padding': 16, 'gap': 12, 'children': children}}
    script = '''"use strict";
function read(){Ouse.storage.get("probe").then(value=>Ouse.ui.setText("status",value?"Stored:73":"Empty"));Ouse.ui.setText("alarms","ALARMS="+Ouse.alarms.list().length);}
Ouse.ui.on("seed","click",()=>{Ouse.storage.set("probe",{marker:"manager-qa",value:73}).then(read);Ouse.alarms.set({id:"probe",title:"QA",weekdays:127,minuteOfDay:1439,utcOffsetMinutes:480,enabled:false});});
Ouse.tasks.onComplete(()=>Ouse.ui.setText("alarms","ALARMS="+Ouse.alarms.list().length));globalThis.app={onResume:read};read();'''
    content = {'manifest.json': json.dumps(manifest).encode(), 'page.json': json.dumps(page).encode(), 'app.js': script.encode(), 'icon.bin': b'OIC1'+struct.pack('<HH', 1, 1)+bytes((160, 80, 40, 255))}
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, data in content.items():
            archive.writestr(name, data)
    data = stream.getvalue()
    bpk.validate(data)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM5')
    parser.add_argument('--output', type=Path, default=Path('manager-check'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    client = bpk.Client(args.port, 921600)
    client.serial.timeout = .005
    report = {'events': []}
    installed = []
    owned = []

    def command(verb, payload='-', timeout=20):
        return client.request(verb, payload, timeout=timeout, attempts=3)

    def resource(verb):
        size, checksum = command(verb).split(':')
        data = bytearray()
        try:
            for offset in range(0, int(size), 256):
                data.extend(bytes.fromhex(command('DBG_READ', f'{offset}:{min(256,int(size)-offset)}')))
        finally:
            command('DBG_FREE')
        assert bpk.crc(data) == int(checksum, 16)
        return data

    def tree():
        nodes = json.loads(resource('DBG_TREE'))['nodes']
        stack = []
        for node in nodes:
            while stack and stack[-1]['depth'] >= node['depth']:
                stack.pop()
            bounds = [20, 44, 300, 439]
            for ancestor in stack+[node]:
                left, top, right, bottom = ancestor['bounds']
                bounds = [max(bounds[0],left),max(bounds[1],top),min(bounds[2],right),min(bounds[3],bottom)]
            node['_clip'] = bounds if bounds[2]-bounds[0]>=5 and bounds[3]-bounds[1]>=5 else None
            stack.append(node)
        return nodes

    def swipe(forward=True):
        start, end = (210, 75) if forward else (155, 370)
        for step in range(9):
            command('DBG_TOUCH',f'160:{round(start+(end-start)*step/8)}:1')
            time.sleep(.025)
        command('DBG_TOUCH',f'160:{end}:0');time.sleep(.75)

    def seek(label):
        for direction in (True, False):
            previous = None
            same = 0
            for attempt in range(24):
                nodes = tree()
                found = [node for node in nodes if node['text']==label and node['_clip']]
                if found:
                    return found[-1]
                signature = [(node['type'],node['bounds']) for node in nodes]
                same = same+1 if signature==previous else 0
                if same>=2:
                    break
                previous=signature;swipe(direction)
        raise AssertionError('Missing '+label+' '+repr(nodes))

    def tap(label):
        node = seek(label);left,top,right,bottom=node['_clip'];horizontal=(left+right)//2;vertical=(top+bottom)//2
        command('DBG_TOUCH',f'{horizontal}:{vertical}:1');time.sleep(.08);command('DBG_TOUCH',f'{horizontal}:{vertical}:0');time.sleep(.65)
        report['events'].append('tap '+label)
        print('tap',label,flush=True)

    def wait_text(label):
        for attempt in range(15):
            nodes=tree()
            if any(node['text']==label for node in nodes):return nodes
            time.sleep(.3)
        raise AssertionError('Missing '+label+' '+repr(nodes))

    def shot(name):
        (args.output/(name+'.jpg')).write_bytes(resource('DBG_SHOT'))
        (args.output/(name+'.json')).write_text(json.dumps(tree(),ensure_ascii=False,indent=2),encoding='utf-8')

    def manager():
        command('DBG_CLOSE','ouse.settings');command('DBG_OPEN','ouse.settings');time.sleep(.5);tap('应用管理');time.sleep(.7)

    def labels():
        return [node['text'] for node in tree()]

    try:
        report['hello']=command('HELLO');assert 'core=0.19.1' in report['hello']
        installed=sorted(filter(None,command('LIST').split(',')))
        assert QA not in installed and CONTROL not in installed, 'QA identity already exists; refusing to overwrite'
        report['before']=installed;report['sd_before']=command('DBG_FINFO','Music/fur-elise-30s.wav')
        report['runtime_before']=command('DBG_RUNTIME')
        packages={QA:package(QA,'QA Target'),CONTROL:package(CONTROL,'QA Control')}
        for app_id,data in packages.items():
            owned.append(app_id);client.install(data);command('DBG_OPEN',app_id);wait_text('Empty');tap('Seed');wait_text('Stored:73');wait_text('ALARMS=1')
        command('BEGIN',f'00000001:{bpk.crc(b"x"):08x}');time.sleep(31)
        report['abandoned_upload_timeout']=command('HELLO')
        manager();shot('list');tap('QA Target')
        expected=len(json.dumps({'marker':'manager-qa','value':73},separators=(',',':')).encode())
        report['target_package_bytes']=len(packages[QA]);report['target_private_bytes']=expected
        wait_text(f'本地数据：{expected} B');wait_text('原生闹钟：1')
        wait_text(f'应用大小：{len(packages[QA])/1024:.1f} KiB');wait_text(f'合计：{(len(packages[QA])+expected)/1024:.1f} KiB');shot('detail')
        tap('清除数据');shot('clear-confirm');tap('取消');wait_text(f'本地数据：{expected} B')
        tap('清除数据');tap('清除数据');wait_text('本地数据：0 B');assert '原生闹钟：1' not in labels();shot('cleared')
        tap('打开应用');wait_text('Empty');wait_text('ALARMS=0')
        command('DBG_OPEN',CONTROL);wait_text('Stored:73');wait_text('ALARMS=1')
        manager();tap('QA Target');tap('卸载');tap('取消');assert QA in command('LIST').split(',')
        tap('卸载');tap('卸载')
        for attempt in range(15):
            if QA not in command('LIST').split(','):break
            time.sleep(.3)
        assert QA not in command('LIST').split(',');owned.remove(QA);shot('uninstalled')
        tap('设置');wait_text('应用大小：包含在系统固件中');shot('protected')
        tap('清除数据');assert not any('清除本地数据和闹钟' in text for text in labels())
        command('DBG_OPEN',CONTROL);wait_text('Stored:73');wait_text('ALARMS=1')
        report['sd_after']=command('DBG_FINFO','Music/fur-elise-30s.wav');assert report['sd_after']==report['sd_before']
        report['runtime_after']=command('DBG_RUNTIME');assert 'errors=0' in report['runtime_after']
        assert command('DBG_CHECK')=='ok=1';report['heap']=command('DBG_HEAP');report['passed']=True
        print('PASS management: exact data/package sizes, cancelled/confirmed clear and uninstall, alarm purge, reopening, control isolation, protected Settings, SD checksum, runtime/heap',flush=True)
    finally:
        for app_id in owned:
            try:command('REMOVE',app_id)
            except Exception as error:report.setdefault('cleanup_errors',[]).append(str(error))
        try:
            report['after']=sorted(filter(None,command('LIST').split(',')))
            if installed:assert report['after']==installed
            command('DBG_HOME')
        finally:
            (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');client.close()


if __name__=='__main__':
    main()
