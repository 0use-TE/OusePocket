"""Author the three 跳跳庄园 maps from distinct terrain and route plans."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PLANS=[
    dict(name='青苹果山谷',difficulty='入门',skin=0,rows=24,depth=99,
         profile=[20,20,19,19,18,18,17,18,18,20],
         flags=[26,66,106,146,186],branches=[2,5,8]),
    dict(name='梯田迷境',difficulty='进阶',skin=1,rows=36,depth=99,
         profile=[31,29,27,25,23,21,19,17,15,13,15,17,19,21,19],
         flags=[22,62,102,142,182,222,262,288],branches=[1,3,5,7,9,11,13]),
    dict(name='云顶试炼',difficulty='挑战',skin=2,rows=44,depth=3,
         profile=[38,36,34,32,30,28,26,24,22,20,18,16,14,12,10,12,14,16,18,20],
         flags=[24,64,104,144,184,224,264,304,344,384],branches=[1,4,7,10,13,16])
]

def build():
    levels=[];packed=bytearray()
    for index,p in enumerate(PLANS):
        terrain=[];platforms=[];spikes=[];fruits=[];gaps=[]
        for section,row in enumerate(p['profile']):
            base=section*20
            # The first map gives wide landings; the last alternates long voids
            # and clustered spikes on floating islands, with an ascending route.
            if index==0:
                gap=(base+12,base+13) if section%2==0 else None
            elif index==1:
                gap=(base+10,base+12+(section%4==0)) if section%2==0 else None
            else:
                gap=(base+10,base+14 if section%3==0 else base+13) if section%3!=2 else None
            if gap:gaps.append(list(gap))
            for a,b in ([(base,gap[0]-1),(gap[1]+1,base+19)] if gap else [(base,base+19)]):
                terrain.append([a,b,row,min(p['rows']-1,row+p['depth']-1)])
            if gap is None:
                for offset in ([10] if index==0 else [9,10] if index==1 else [8,9,10]):
                    spikes.append([(base+offset)*16,row*16])
            for offset in [2,6,17]:fruits.append([(base+offset)*16+8,row*16-18])
            if section in p['branches']:
                for a,b,rise in [(3,6,3),(9,12,5),(15,18,3)]:
                    platforms.append([base+a,base+b,row-rise])
                    fruits.append([(base+a+1)*16+8,(row-rise)*16-16])
        columns=len(p['profile'])*20
        checkpoints=[dict(id='flag'+str(n),x=col*16+8,y=p['profile'][col//20]*16)
                     for n,col in enumerate(p['flags'])]
        for c in checkpoints:
            col=c['x']//16
            assert not any(a<=col<=b for a,b in gaps)
            assert not any(abs(c['x']-x)<32 for x,y in spikes)
        # Precompute immutable binary cells offline. The device must not expand
        # tens of thousands of cells inside a 50 ms input callback.
        cells=[['0']*columns for _ in range(p['rows'])]
        for a,b,top,bottom in terrain:
            for row in range(top,bottom+1):
                for col in range(a,b+1):cells[row][col]='1'
        for a,b,row in platforms:
            for col in range(a,b+1):
                if cells[row][col]=='0':cells[row][col]='2'
        tile_offset=len(packed)
        packed.extend(int(value) for row in cells for value in row)
        levels.append(dict(revision=3,name=p['name'],difficulty=p['difficulty'],skin=p['skin'],tileOffset=tile_offset,
                           columns=columns,rows=p['rows'],terrain=terrain,platforms=platforms,
                           spikes=spikes,fruits=fruits,checkpoints=checkpoints,
                           spawn=[48,p['profile'][0]*16],exit=[(columns-3)*16+8,p['profile'][-1]*16]))
    target=ROOT/'apps/跳跳庄园/assets/levels.json'
    (target.parent/'tiles.bin').write_bytes(packed)
    target.write_text(json.dumps(levels,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    for p in levels:print(p['name'],p['columns']*16,'x',p['rows']*16,'fruits',len(p['fruits']),'flags',len(p['checkpoints']))

if __name__=='__main__':build()
