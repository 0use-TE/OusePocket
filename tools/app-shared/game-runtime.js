// Shared foreground game scheduler: one UI-paced frame, no frame backlog.
function createGameRuntime(onFrame,maxFps=25){
 let id=0,running=false,next=0,since=0,frames=0,fps=0;const period=maxFps>0?1000/maxFps:0;
 function frame(){id=0;if(!running)return;const now=Date.now();if(now>=next){const target=next+period;next=target>now?target:now+period;frames++;if(now-since>=1000){fps=Math.round(frames*1000/(now-since));frames=0;since=now;}onFrame();}if(running&&!id)id=Ouse.timers.requestFrame(frame);}
 return {start(){if(running)return;running=true;next=since=Date.now();frames=fps=0;id=Ouse.timers.requestFrame(frame);},stop(){running=false;if(id)Ouse.timers.clear(id);id=0;fps=0;},get running(){return running;},get fps(){return fps;}};
}
