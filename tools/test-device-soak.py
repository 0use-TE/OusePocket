"""Exercise mixed foreground/background operation and repeated cleanup on hardware."""
import argparse,importlib.util,json,sys,time
from pathlib import Path
from device import Device
spec=importlib.util.spec_from_file_location('system_ui',Path(__file__).with_name('test-device-system.py'))
system=importlib.util.module_from_spec(spec)
spec.loader.exec_module(system)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM10')
    parser.add_argument('--elf')
    parser.add_argument('--output')
    parser.add_argument('--rounds',type=int,default=60)
    parser.add_argument('--idle',type=float,default=10)
    args=parser.parse_args()
    if not 10 <= args.rounds <= 200 or args.rounds%10:parser.error('--rounds must be a multiple of ten, 10..200')
    if not 0 <= args.idle <= 60:parser.error('--idle must be 0..60')
    report={'status':'running','started':time.time(),'rounds':args.rounds,'events':[],'batches':[]}
    with Device(args.port,args.output,args.elf) as device:
        def save():
            (device.output/'soak-result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        device.request('HELLO',timeout=10)
        ui=system.UI(device,report,save)
        def clear():
            ui.home()
            if ui.state()['live']:
                ui.overview()
                ui.tap_label(('全部清除','Clear all'))
                ui.wait_state(lambda s:s['live']==0 and not s['recents'] and not s['active'],'Cleanup did not release all applications')
            time.sleep(.5)
        try:
            # Warm font, JS, snapshots and settings allocations before comparing
            # identical, fully closed application states.
            device.stress(3)
            (device.output/'stress.json').replace(device.output/'stress-warmup.json')
            clear()
            baseline=ui.checkpoint('empty_baseline')
            report['baseline']=baseline
            for completed in range(10,args.rounds+1,10):
                device.stress(10)
                (device.output/'stress.json').replace(device.output/f'stress-through-{completed}.json')
                clear()
                sample=ui.checkpoint('empty_after_'+str(completed))
                delta={key:sample['heap'][key]-baseline['heap'][key] for key in ('internal_free','psram_free','tasks')}
                report['batches'].append({'rounds_completed':completed,'checkpoint':sample,'delta':delta})
                save()
                assert delta['internal_free']>=-4096,delta
                assert delta['psram_free']>=-8192,delta
                assert delta['tasks']==0,delta
                print('PASS mixed cleanup',completed,'delta',delta,flush=True)
                time.sleep(args.idle)
            report['final']=ui.checkpoint('final_after_idle')
            report['flush']=device.request('DBG_FLUSH','status')
            report['lcd']=device.request('DBG_LCD','status')
            assert report['flush'].startswith('enabled=1,')
            assert report['lcd']=='enabled=1,skipped=0'
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
