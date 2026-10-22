"""Stop native desktop games while allowing older firmware without every app."""
import time
from device import ProtocolError
from native import status

def stop_previous(d):
    d.request('DBG_HOME')
    for app in ('ouse.native','ouse.quake2','ouse.doom','ouse.nes'):
        try:d.request('DBG_CLOSE',app)
        except ProtocolError as e:
            # An older firmware can lack an optional desktop registration.
            # Transport failures and other device errors must still propagate.
            if str(e)!='DBG_CLOSE: ESP_ERR_NOT_FOUND':raise
    end=time.monotonic()+15
    while status(d)['active']:
        if time.monotonic()>end:raise TimeoutError('Previous native session is still stopping')
        time.sleep(.1)
