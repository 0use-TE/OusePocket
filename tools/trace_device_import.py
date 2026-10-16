"""Load the shared raw trace collector (whose command-line filename has a hyphen)."""
import importlib.util
from pathlib import Path
_spec=importlib.util.spec_from_file_location("ouse_uart_trace",Path(__file__).with_name("trace-device.py"))
_module=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_module)
Capture=_module.Capture
