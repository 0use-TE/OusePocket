"""Reproduce local game-47931 resource/script extraction. Does not run the SWF."""
from pathlib import Path
import argparse,collections,hashlib,json,struct,subprocess,urllib.request,zipfile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/game-47931'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--java',required=True,help='Path to a Java runtime')
args=parser.parse_args()
OUT.mkdir(parents=True,exist_ok=True)
swf=OUT/'original.swf'
url='https://sda.4399.com/4399swf/upload_swf/ftp3/haibo/20110124/1.swf'
if not swf.exists():
    request=urllib.request.Request(url,headers={'Referer':'https://www.4399.com/flash/47931_1.htm','User-Agent':'Mozilla/5.0'})
    with urllib.request.urlopen(request,timeout=90) as response:swf.write_bytes(response.read())
b=swf.read_bytes()
assert hashlib.sha256(b).hexdigest()=='3f633169368ff02907553f3145775379c3c7409489f9d9f0f67aa954a5559a6e','Source changed; inspect before proceeding'
assert b[:3]==b'FWS'
jar=OUT/'ffdec/ffdec.jar'
if not jar.exists():
    u='https://github.com/jindrapetrik/jpexs-decompiler/releases/download/version26.4.0/ffdec_26.4.0.zip'
    with urllib.request.urlopen(u,timeout=90) as response:(OUT/'ffdec.zip').write_bytes(response.read())
    destination=(OUT/'ffdec').resolve()
    with zipfile.ZipFile(OUT/'ffdec.zip') as z:
        for item in z.infolist():
            assert (destination/item.filename).resolve().is_relative_to(destination),'Unsafe archive member'
        z.extractall(destination)
def ffdec(log,*arguments):
    with (OUT/log).open('w',encoding='utf-8') as output:
        subprocess.run([args.java,'-Djava.awt.headless=true','-Xmx2g','-Xss32m','-jar',str(jar),*map(str,arguments)],stdout=output,stderr=subprocess.STDOUT,check=True,cwd=ROOT,timeout=600)
ffdec('export.log','-export','script,image,sound',OUT/'export',swf)
ffdec('deob.log','-deobfuscate','traps',swf,OUT/'clean.swf')
ffdec('clean-export.log','-export','script',OUT/'clean-export',OUT/'clean.swf')
ffdec('sprites.log','-selectid','52,3562,3595,3598,3532,3502,2227,2177,2200,1057,1060,502,496,379,376,493,412,466,439','-export','sprite',OUT/'sprites',OUT/'clean.swf')
# SymbolClass table: retain names to original character IDs for future baking.
nb=b[8]>>3;pos=8+(5+4*nb+7)//8+4;symbols={}
while pos<len(b):
    header=struct.unpack_from('<H',b,pos)[0];pos+=2;tag=header>>6;size=header&63
    if size==63:size=struct.unpack_from('<I',b,pos)[0];pos+=4
    data=b[pos:pos+size]
    if tag==76:
        cursor=2
        for i in range(struct.unpack_from('<H',data)[0]):
            cid=struct.unpack_from('<H',data,cursor)[0];cursor+=2;end=data.index(0,cursor)
            symbols[data[cursor:end].decode('utf-8')]=cid;cursor=end+1
    pos+=size
    if not tag:break
(OUT/'symbols.json').write_text(json.dumps(symbols,ensure_ascii=False,indent=2),encoding='utf-8')
print('Local resources and reconstructed scripts:',OUT)
print('No public-domain claim; original source page attributes copyright to authors.')
