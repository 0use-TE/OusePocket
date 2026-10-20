// Small reusable game loop; animation frames stay in the native sprite layer.
function createGameLoop(step,render){
 let timer=0,last=0;
 return {start(){if(timer)return;last=Date.now();timer=Ouse.timers.setInterval(()=>{const now=Date.now(),dt=Math.min(.1,(now-last)/1000);last=now;step(dt);render();},50);},stop(){if(timer)Ouse.timers.clear(timer);timer=0;}};
}
