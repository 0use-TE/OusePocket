"""Check the real system clock against PC time and localized quick-sheet dates."""
import argparse,importlib.util,json,re,time
from pathlib import Path
from datetime import datetime,timezone,timedelta
from device import Device
spec=importlib.util.spec_from_file_location('system_ui',Path(__file__).with_name('test-device-system.py'))
system=importlib.util.module_from_spec(spec)
spec.loader.exec_module(system)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM10')
    parser.add_argument('--elf')
    parser.add_argument('--output')
    args=parser.parse_args()
    report={'started':time.time(),'status':'running','observations':[]}
    tz=timezone(timedelta(hours=8))
    with Device(args.port,args.output,args.elf) as device:
        def save():
            (device.output/'clock-result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        device.request('HELLO',timeout=10)
        ui=system.UI(device)
        original=ui.state()['locale']
        try:
            for locale in (0,1):
                ui.open_app('ouse.api-test')
                ui.set_locale(locale)
                ui.home()
                device.swipe(ui.width//2,ui.dp(8),ui.width//2,ui.dp(300),200)
                assert ui.state()['quick'],'Quick sheet did not open'
                texts=[n['text'] for n in ui.tree() if n['type']=='label']
                now=datetime.now(tz)
                clocks=[text for text in texts if re.fullmatch(r'\d{2}:\d{2}',text)]
                assert len(clocks)>=2,('Status and quick clocks must both be synced',texts)
                for clock in clocks:
                    hour,minute=map(int,clock.split(':'))
                    difference=(hour*60+minute-(now.hour*60+now.minute)+720)%1440-720
                    assert abs(difference)<=1,('Board clock diverges from PC time',clock,now.isoformat())
                expected=(f'{now.month}月{now.day}日' if locale==0 else now.strftime('%b %d, %a'))
                assert expected in texts,('Localized quick date mismatch',expected,texts)
                report['observations'].append({'locale':locale,'pc_time':now.isoformat(),'clocks':clocks,'date':expected})
                save()
                device.screenshot(device.output/('quick-'+('zh' if locale==0 else 'en')+'.jpg'))
                ui.home()
            report['status']='passed'
        except BaseException as error:
            report['status']='failed'
            report['error']=repr(error)
            raise
        finally:
            if ui.alive():
                ui.home()
                ui.open_app('ouse.api-test')
                ui.set_locale(original)
                ui.home()
                report['locale_restored']=ui.state()['locale']==original
            report['finished']=time.time()
            save()
            print('Evidence:',device.output.resolve(),flush=True)


if __name__=='__main__':main()
