"""Synthesize original Orchard music and effects offline; no JS sample generation."""
import json,math,struct,wave
from pathlib import Path

APP=Path(__file__).resolve().parents[1]/'apps/跳跳庄园'
def frequency(note):return 440*2**((note-69)/12)
def triangle(phase):return 1-4*abs(phase%1-.5)
def encode(samples):return b''.join(struct.pack('<h',round(max(-32767,min(32767,x)))) for x in samples)
def build():
    target=APP/'assets';rate=8000;beat=1/3;duration=16*beat
    # Four original bars, C / Am / F / G; eighth-note melody over soft bass/arpeggio.
    melody=[72,76,79,76,74,72,67,71,72,76,81,79,76,74,72,69,
            69,72,77,76,74,72,69,72,71,74,79,77,76,74,71,67]
    chords=[[48,64,67],[45,60,64],[41,60,65],[43,59,62]]
    samples=[]
    for i in range(round(duration*rate)):
        t=i/rate;step=min(31,int(t/(beat/2)));local=t-step*beat/2
        envelope=min(1,local/.006)*max(0,1-local/(beat/2))**.55
        lead=triangle(t*frequency(melody[step]))*2100*envelope
        chord=chords[min(3,int(t/(4*beat)))];b=t%beat
        bass=math.sin(2*math.pi*t*frequency(chord[0]))*1100*min(1,b/.008)*max(0,1-b/beat)
        arp=math.sin(2*math.pi*t*frequency(chord[1+step%2]+12))*650*envelope
        samples.append(lead+bass+arp)
    with wave.open(str(target/'music.wav'),'wb') as f:
        f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes(encode(samples))
    bank=bytearray();clips={}
    plans={
        'jump':(.08,[64,76]),'double':(.10,[76,88]),'fruit':(.075,[84,91]),
        'hurt':(.14,[55,38]),'flag':(.18,[72,76,79]),'clear':(.24,[72,76,79,84])}
    for name,(length,notes) in plans.items():
        pcm=[];phase=0;count=round(length*16000)
        for i in range(count):
            p=i/count;t=i/16000
            if name in ['jump','double','hurt']:
                hz=frequency(notes[0])*(frequency(notes[-1])/frequency(notes[0]))**p
            else:hz=frequency(notes[min(len(notes)-1,int(p*len(notes)))])
            phase+=hz/16000
            envelope=min(1,t/.004)*min(1,(length-t)/.012)*(1-.4*p)
            pcm.append(triangle(phase)*7500*envelope)
        data=encode(pcm);clips[name]={'offset':len(bank),'length':len(data)};bank.extend(data)
    (target/'sounds.pcm').write_bytes(bank)
    (target/'audio.json').write_text(json.dumps(clips,separators=(',',':'))+'\n',encoding='utf-8')
    print('Original audio:',len(samples)/rate,'seconds, music',len(samples)*2+44,'bytes, effects',len(bank),'bytes')
if __name__=='__main__':build()
