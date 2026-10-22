"""Regression for launching games on firmware without an optional NES app."""
import unittest
from unittest.mock import patch
from device import ProtocolError
import game_session

class DeviceStub:
    def __init__(self,error=None):self.calls=[];self.error=error
    def request(self,verb,payload='-'):
        self.calls.append((verb,payload))
        if payload=='ouse.nes':raise self.error or ProtocolError('DBG_CLOSE: ESP_ERR_NOT_FOUND')

class SessionTests(unittest.TestCase):
    def test_missing_nes_still_waits_for_native_cleanup(self):
        d=DeviceStub()
        with patch.object(game_session,'status',side_effect=[{'active':True},{'active':False}]) as state,patch.object(game_session.time,'sleep'):
            game_session.stop_previous(d)
        self.assertEqual(state.call_count,2)
        self.assertEqual(d.calls[0],('DBG_HOME','-'))
        self.assertIn(('DBG_CLOSE','ouse.quake2'),d.calls)
        self.assertIn(('DBG_CLOSE','ouse.doom'),d.calls)

    def test_other_device_errors_are_not_ignored(self):
        with self.assertRaises(ProtocolError):
            game_session.stop_previous(DeviceStub(ProtocolError('DBG_CLOSE: ESP_FAIL')))

    def test_transport_errors_are_not_ignored(self):
        with self.assertRaises(TimeoutError):
            game_session.stop_previous(DeviceStub(TimeoutError('Serial disconnected')))

if __name__=='__main__':unittest.main()
