"""Stress real LCD repaint without disabling Wi-Fi, DMA, or CPU idle."""
from pathlib import Path
import argparse, json, sys, time
from device import Device


def fields(text):
    return {k: int(v) for k, v in (part.split('=', 1) for part in text.split(','))}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM10')
    parser.add_argument('--elf')
    parser.add_argument('--output')
    parser.add_argument('--rounds',type=int,default=1000)
    args=parser.parse_args()
    if not 100 <= args.rounds <= 20000:parser.error('--rounds must be 100..20000')
    report={'started':time.time(),'status':'running','samples':[],'rounds':args.rounds}
    with Device(args.port,args.output,args.elf) as device:
        def save():
            (device.output/'repaint-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        def sample(round):
            check=device.request('DBG_CHECK',timeout=20,attempts=1)
            assert check=='ok=1',check
            data={'round':round,'time':time.time(),'heap':fields(device.request('DBG_HEAP',timeout=15)),
                  'flush':fields(device.request('DBG_FLUSH','status')),'state':device.state()}
            report['samples'].append(data)
            save()
            return data
        device.request('HELLO',timeout=8)
        assert fields(device.request('DBG_LCD','status'))=={'enabled':1,'skipped':0}
        assert fields(device.request('DBG_FLUSH','status'))['enabled']==1
        device.request('DBG_HOME')
        for _ in range(30):
            device.request('DBG_REPAINT',attempts=1)
            time.sleep(.04)
        before=sample(0)
        try:
            for i in range(1,args.rounds+1):
                assert device.request('DBG_REPAINT',attempts=1)=='queued=1'
                time.sleep(.04)
                if i % 200 == 0 or i==args.rounds:
                    state=sample(i)
                    assert not state['state']['active'] and not state['state']['recents'] and not state['state']['quick'],state
                    print('PASS real LCD repaint',i,'heap',state['heap']['internal_free'],state['heap']['psram_free'],flush=True)
            time.sleep(.5)
            after=sample(args.rounds)
            report['actual_flush_waits']=after['flush']['waits']-before['flush']['waits']
            assert report['actual_flush_waits'] >= args.rounds,report
            assert after['flush']['enabled']==1
            assert fields(device.request('DBG_LCD','status'))=={'enabled':1,'skipped':0}
            report['free_delta']={k:after['heap'][k]-before['heap'][k] for k in ('internal_free','psram_free','tasks')}
            assert report['free_delta']['internal_free']>=-1024,report['free_delta']
            assert report['free_delta']['psram_free']>=-1024,report['free_delta']
            assert report['free_delta']['tasks']==0,report['free_delta']
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
