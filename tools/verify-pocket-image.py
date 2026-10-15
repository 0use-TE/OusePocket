"""Read the actual LittleFS image and compare every file with the staging tree.

Run with build/littlefs_py_venv/Scripts/python.exe (created by the IDF build).
"""
from pathlib import Path
import json
from littlefs import LittleFS
from littlefs.context import UserContext
ROOT=Path(__file__).resolve().parents[1]
project=ROOT/'examples/brookesia-minimal'
image=(project/'build/littlefs_data.bin').read_bytes()
ctx=UserContext(len(image));ctx.buffer[:]=image
fs=LittleFS(context=ctx,block_size=4096,block_count=len(image)//4096,name_max=64)
count=0
for directory,dirs,files in fs.walk('/'):
    for name in files:
        path=directory.rstrip('/')+'/'+name
        with fs.open(path,'rb') as stream:data=stream.read()
        staged=project/'littlefs'/path.lstrip('/')
        assert staged.is_file() and staged.read_bytes()==data,f'Stale image file: {path}'
        count+=1
expected=sum(p.is_file() for p in (project/'littlefs').rglob('*'))
assert count==expected,(count,expected)
apps=set(fs.listdir('/apps'))
assert apps=={'brookesia.general.settings'},f'Unexpected preinstalled apps: {apps}'
for path,source in [('apps/brookesia.general.settings/res','components/brookesia_app_settings/package/res'),
                    ('system/super/shell','components/brookesia_system_super/resource/shell')]:
    assert (project/'littlefs'/path).is_dir(),path
    for original in (ROOT/source).rglob('*'):
        if original.is_file():assert (project/'littlefs'/path/original.relative_to(ROOT/source)).read_bytes()==original.read_bytes(),original
print(f'PASS actual LittleFS: {count} files byte-identical to staging; Settings/Shell source matches; optional apps absent')
