// Native playback/mixing owns PCM work. Only control messages run in the game loop.
function createSurvivorAudio(host){
 const clips=JSON.parse(host.assets.readText('audio'));
 let wanted=false,closed=false,nextCheck=0,retryAt=0,nextReport=1000,effects=0;
 function update(now){
  if(closed||!wanted||now<nextCheck)return;nextCheck=now+250;
  const state=host.media.get();
  if(state&&state.active){
   if(state.paused)host.media.resume();
   if(now>=nextReport){nextReport=now+10000;console.log('SURVIVOR_AUDIO '+JSON.stringify({positionMs:state.positionMs||0,effects,error:state.errorCode||0}));}
   return;
  }
  if(now<retryAt)return;retryAt=now+2000;
  if(host.media.play({asset:'music',loop:true}))host.media.setInfo({title:'荒野生存',artist:'荒野行军曲'});
 }
 function sync(enabled){
  if(closed)return;
  if(wanted===enabled)return;wanted=enabled;nextCheck=0;
  if(!wanted)host.media.pause();else update(Date.now());
 }
 function effect(name){
  if(closed||!wanted)return false;const clip=clips[name];if(!clip)return false;
  const accepted=host.media.effect({asset:'sounds',offset:clip.offset,length:clip.length,gain:85});
  if(accepted)effects++;return accepted;
 }
 return {sync,update,effect,close(){if(closed)return;closed=true;wanted=false;host.media.stop();},get effects(){return effects;}};
}
