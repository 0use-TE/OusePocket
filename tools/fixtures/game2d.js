function assert(ok,message='assertion'){if(!ok)throw Error(message);}
function reference(a,t,h,dt){
 for(let i=0;i<a.length/8;i++){
  const k=i*8;h[i]=0;if(!a[k+6])continue;const ax=a[k],ay=a[k+1];a[k]+=a[k+2]*dt;a[k+1]+=a[k+3]*dt;a[k+4]-=dt;
  const dx=a[k]-ax,dy=a[k+1]-ay,len=dx*dx+dy*dy;
  for(let j=0;j<t.length/4;j++){
   const q=j*4;if(!t[q+3]||((a[k+7]>>>0)&(1<<j)))continue;
   const u=len?Math.max(0,Math.min(1,((t[q]-ax)*dx+(t[q+1]-ay)*dy)/len)):0;
   const x=t[q]-ax-dx*u,y=t[q+1]-ay-dy*u,r=t[q+2]+a[k+5];if(x*x+y*y<r*r)h[i]|=1<<j;
  }
 }
}
const a=new Float64Array(128*8),t=new Float64Array(32*4),h=new Uint32Array(128);
let rng=123;
function random(){rng=(Math.imul(rng,1664525)+1013904223)>>>0;return rng/4294967296;}
for(let trial=0;trial<1000;trial++){
 for(let i=0;i<128;i++)a.set([random()*800,random()*400,random()*1200-600,random()*1200-600,2,3,random()<.9?1:0,(random()*0xffffffff)>>>0],i*8);
 for(let j=0;j<32;j++)t.set([random()*800,random()*400,5+random()*25,1],j*4);
 const b=a.slice(),expected=new Uint32Array(128);reference(b,t,expected,1/60);nativeStep(a.buffer,t.buffer,h.buffer,1/60);
 assert(a.every((v,i)=>v===b[i]),'integration parity');assert(h.every((v,i)=>v===expected[i]),'swept overlap parity');
}
// Tunnelling, exact tangency, stationary projectiles and bit 31.
a.fill(0);t.fill(0);a.set([0,0,1200,0,2,1,1,0]);t.set([10,0,1,1],31*4);nativeStep(a.buffer,t.buffer,h.buffer,1/60);assert(h[0]===0x80000000,'bit 31/tunnelling');
a[7]=0x80000000;nativeStep(a.buffer,t.buffer,h.buffer,0);assert(h[0]===0,'excluded');
a.set([0,0,0,0,1,1,1,0]);t.set([2,0,1,1],31*4);nativeStep(a.buffer,t.buffer,h.buffer,0);assert(h[0]===0,'strict tangency');
function rejects(fn){let failed=false;try{fn();}catch(e){failed=true;}assert(failed,'must reject');}
for(const dt of [NaN,Infinity,-.1,.126,'0.1'])rejects(()=>nativeStep(a.buffer,t.buffer,h.buffer,dt));
rejects(()=>nativeStep(a.buffer,t.buffer,a.buffer,.01));rejects(()=>nativeStep(new ArrayBuffer(65),t.buffer,h.buffer,.01));
rejects(()=>nativeStep(new ArrayBuffer(129*64),t.buffer,new ArrayBuffer(129*4),.01));
rejects(()=>nativeStep(a.buffer,new ArrayBuffer(33*32),h.buffer,.01));
a[8*127]=NaN;h.fill(123);const before=a.slice();rejects(()=>nativeStep(a.buffer,t.buffer,h.buffer,.01));assert(a.every((v,i)=>Object.is(v,before[i]))&&h.every(v=>v===123),'atomic refusal');a[8*127]=0;
const detached=new ArrayBuffer(64);detachBuffer(detached);rejects(()=>nativeStep(detached,t.buffer,new ArrayBuffer(4),0));
if(typeof SharedArrayBuffer==='function')rejects(()=>nativeStep(new SharedArrayBuffer(64),t.buffer,new ArrayBuffer(4),0));
print('PASS 1,000 randomized batches, swept hits, tangency, masks, bounds, alias and atomic validation');
// Host QuickJS timing includes the real binding and full validation.
for(let i=0;i<128;i++)a.set([0,0,30,0,10000,3,1,0],i*8);
for(let j=0;j<32;j++)t.set([j*15,j%3*10,10,1],j*4);
function measure(fn){const start=now();for(let i=0;i<3000;i++)fn();return now()-start;}
measure(()=>nativeStep(a.buffer,t.buffer,h.buffer,.001));
const nativeMs=measure(()=>nativeStep(a.buffer,t.buffer,h.buffer,.001)),jsMs=measure(()=>reference(a,t,h,.001));
print(JSON.stringify({scope:'host QuickJS kernel, 128 projectiles x 32 targets; not hardware FPS',nativeMs,jsMs,speedup:jsMs/nativeMs}));
