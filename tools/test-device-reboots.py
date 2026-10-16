"""Repeat UART RTS/EN resets and verify image identity, data and preferences."""
import argparse,hashlib,importlib.util,json,re,time
from pathlib import Path
from device import Device
spec=importlib.util.spec_from_file_location('system_ui',Path(__file__).with_name('test-device-system.py'))
system=importlib.util.module_from_spec(spec)
spec.loader.exec_module(system)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM10')
    parser.add_argument('--elf',default=str(Path(__file__).resolve().parents[1]/'build/ouse_core_desktop.elf'))
    parser.add_argument('--output')
    parser.add_argument('--rounds',type=int,default=3)
    args=parser.parse_args()
    if not 1 <= args.rounds <= 10:parser.error('--rounds must be 1..10')
    sha=hashlib.sha256(Path(args.elf).read_bytes()).hexdigest()
    report={'status':'running','started':time.time(),'elf_sha256':sha,'resets':[]}
    with Device(args.port,args.output,args.elf) as device:
        def save():
            (device.output/'reboots-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        device.request('HELLO',timeout=10)
        ui=system.UI(device)
        state=ui.state()
        original={key:state[key] for key in ('theme','locale','font')}
        original['brightness']=ui.read_brightness()
        ids=device.request('LIST')
        report['original_preferences']=original
        report['installed_ids']=ids
        try:
            for index in range(args.rounds):
                start=device.capture.bytes
                device.ready=False
                try:
                    device.port.setRTS(True)
                    device.port.setDTR(device.port.dtr)
                    time.sleep(.1)
                finally:
                    device.port.setRTS(False)
                    device.port.setDTR(device.port.dtr)
                hello=device.request('HELLO',timeout=10,attempts=4)
                assert 'storage=ready' in hello and 'debug=1' in hello,hello
                # Observe delayed C6 startup as well as the desktop/installer.
                time.sleep(10)
                assert not device.rebooted and not device.capture.panic
                assert device.request('LIST')==ids,'Persisted package list changed'
                state=ui.state()
                assert all(state[key]==original[key] for key in ('theme','locale','font')),state
                assert ui.read_brightness()==original['brightness'],'Brightness did not persist'
                ui.home()
                assert device.request('DBG_CHECK',timeout=20,attempts=1)=='ok=1'
                with (device.output/'uart.raw.log').open('rb') as raw:
                    raw.seek(start)
                    boot=raw.read()
                assert b'ESP-ROM:' in boot,'Reset did not produce a ROM boot'
                assert re.search(rb'ELF file SHA256: +'+sha[:9].encode(),boot),'Booted image fingerprint does not match the supplied ELF'
                assert b'[OuseFault' not in boot,'Boot recovered an earlier exception record'
                assert b'WDT_RESET' not in boot,'Unexpected watchdog restart'
                record={'round':index+1,'raw_offset':start,'boot_bytes':len(boot),'hello':hello,'state':state,
                        'heap':device.request('DBG_HEAP',timeout=15)}
                report['resets'].append(record)
                save()
                print('PASS RTS/EN reset',index+1,'same image, saved settings and BPKs; no panic',flush=True)
            if ui.state()['live']:
                ui.overview()
                ui.tap_label(('全部清除','Clear all'))
                ui.wait_state(lambda s:s['live']==0 and not s['active'] and not s['recents'],'Final cleanup failed')
            report['final']=ui.state()
            device.screenshot(device.output/'desktop.jpg')
            report['status']='passed'
        except BaseException as error:
            report['status']='failed'
            report['error']=repr(error)
            raise
        finally:
            report['finished']=time.time()
            save()
            print('Evidence:',device.output.resolve(),flush=True)


if __name__=='__main__':main()
