// BEGIN shared game loop
// Small reusable game loop; animation frames stay in the native sprite layer.
function createGameLoop(step,render){
 let timer=0,last=0;
 return {start(){if(timer)return;last=Date.now();timer=Ouse.timers.setInterval(()=>{const now=Date.now(),dt=Math.min(.1,(now-last)/1000);last=now;step(dt);render();},50);},stop(){if(timer)Ouse.timers.clear(timer);timer=0;}};
}
// END shared game loop
"use strict";
// The shared engine.js source is embedded above; the BPK is standalone.
let x=20,y=40,targetX=20,targetY=40,w=280,h=240,score=0,collected=[false,false,false],closed=false,lastMoving=false;
const coinPositions=[[.27,.25],[.72,.52],[.45,.78]];
function patches(){return [{id:"hero",x:Math.round(x),y:Math.round(y)}];}
function layout(){
 const d=Ouse.display.get(),width=Math.floor(d.contentWidthDp),height=Math.floor(d.contentHeightDp);w=width-16;h=Math.max(80,height-124);
 const frames=[{id:"heading",x:8,y:10,width:width-176,height:28},{id:"score",x:width-168,y:12,width:64,height:28},{id:"rotate",x:width-96,y:4,width:88,height:40},{id:"field",x:8,y:52,width:w,height:h},{id:"hint",x:8,y:height-64,width:width-16,height:22},{id:"save",x:8,y:height-42,width:112,height:40},{id:"reset",x:width-120,y:height-42,width:112,height:40}];
 Ouse.ui.layout(frames);x=Math.min(Math.max(0,x),w-24);y=Math.min(Math.max(0,y),h-32);targetX=x;targetY=y;
 const actors=patches();for(let i=0;i<3;i++)actors.push({id:"coin"+i,x:Math.round((w-24)*coinPositions[i][0]),y:Math.round((h-32)*coinPositions[i][1])});Ouse.sprites.update(actors);
}
function render(){Ouse.sprites.update(patches());}
function step(dt){
 const dx=targetX-x,dy=targetY-y,distance=Math.sqrt(dx*dx+dy*dy),moving=distance>1,speed=Math.min(distance,90*dt);
 if(moving){x+=dx/distance*speed;y+=dy/distance*speed;}
 if(moving!==lastMoving){lastMoving=moving;Ouse.sprites.update([{id:"hero",playing:moving,frame:0}]);}
 for(let i=0;i<3;i++)if(!collected[i]){
  const cx=(w-24)*coinPositions[i][0],cy=(h-32)*coinPositions[i][1];
  if(Math.abs(x-cx)<20&&Math.abs(y-cy)<25){collected[i]=true;++score;Ouse.ui.hide("coin"+i);Ouse.ui.setText("score",score+" / 3");if(score===3)Ouse.ui.setText("hint","全部收集完成！");}
 }
}
// A single optional loop; native sprites own their animation frames.
const loop=createGameLoop(step,render);
function aim(event){targetX=Math.max(0,Math.min(w-24,event.x-12));targetY=Math.max(0,Math.min(h-32,event.y-16));}
Ouse.ui.on("field","pointerdown",aim);Ouse.ui.on("field","pointermove",aim);
Ouse.ui.on("rotate","click",()=>Ouse.display.setOrientation(Ouse.display.get().orientation==="portrait"?"landscape":"portrait"));
Ouse.ui.on("reset","click",()=>{score=0;collected=[false,false,false];x=targetX=20;y=targetY=40;for(let i=0;i<3;i++)Ouse.ui.show("coin"+i);Ouse.ui.setText("score","0 / 3");Ouse.ui.setText("hint","点击草地移动，收集金币");render();});
Ouse.ui.on("save","click",()=>{
 const data=JSON.stringify({x,y,score,collected}),bytes=new Uint8Array(data.length);for(let i=0;i<data.length;i++)bytes[i]=data.charCodeAt(i);
 Ouse.sd.write("save.json",bytes.buffer).then(()=>{if(!closed)Ouse.ui.setText("hint","进度已保存到专属 SD 目录");}).catch(()=>{if(!closed)Ouse.ui.setText("hint","保存失败，请检查 SD 卡");});
});
Ouse.display.onChange(layout);globalThis.app={onPause(){loop.stop();},onResume(){layout();loop.start();},onClose(){closed=true;loop.stop();}};
layout();loop.start();
Ouse.sd.read("save.json",0,4096).then(buffer=>{
 if(closed)return;let text="";for(const b of new Uint8Array(buffer))text+=String.fromCharCode(b);const state=JSON.parse(text);
 if(!state||!Number.isFinite(state.x)||!Number.isFinite(state.y)||!Array.isArray(state.collected)||state.collected.length!==3)return;
 collected=state.collected.map(Boolean);score=collected.filter(Boolean).length;x=targetX=Math.max(0,Math.min(w-24,state.x));y=targetY=Math.max(0,Math.min(h-32,state.y));
 for(let i=0;i<3;i++)if(collected[i])Ouse.ui.hide("coin"+i);Ouse.ui.setText("score",score+" / 3");render();
}).catch(()=>{});
