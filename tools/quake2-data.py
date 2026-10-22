"""Split the local Quake II demo into base and optional sound-only PAKs."""
import argparse
import hashlib
from pathlib import Path
import struct

def prepare(source,target,sounds_only=False):
    if source.resolve()==target.resolve():raise ValueError('Use a separate output file')
    total=source.stat().st_size
    with source.open('rb') as f:
        header=f.read(12)
        if len(header)!=12:raise ValueError('Truncated PAK')
        magic,offset,size=struct.unpack('<4sII',header)
        if magic!=b'PACK' or size%64 or size>4*1024*1024 or offset>total or size>total-offset:
            raise ValueError('Invalid PAK directory')
        f.seek(offset);directory=f.read(size);entries=[]
        for i in range(0,size,64):
            name,pos,length=struct.unpack_from('<56sII',directory,i)
            if pos>total or length>total-pos:raise ValueError('Invalid PAK entry')
            if (name.startswith(b'sound/') if sounds_only else not name.startswith((b'sound/',b'demos/'))):entries.append((name,pos,length))
        target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as dest:
            dest.write(b'\0'*12);records=[]
            for name,pos,length in entries:
                records.append(struct.pack('<56sII',name,dest.tell(),length));f.seek(pos)
                remaining=length
                while remaining:
                    data=f.read(min(remaining,1024*1024))
                    if not data:raise ValueError('Truncated PAK entry')
                    dest.write(data);remaining-=len(data)
            offset=dest.tell();dest.write(b''.join(records));dest.seek(0)
            dest.write(struct.pack('<4sII',b'PACK',offset,len(records)*64))
    print(target,target.stat().st_size,'bytes')
    print('SHA256',hashlib.sha256(target.read_bytes()).hexdigest())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('target',type=Path)
    p.add_argument('--sounds-only',action='store_true')
    a=p.parse_args();prepare(a.source,a.target,a.sounds_only)
