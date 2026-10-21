// Shared foreground game scheduler: one UI-paced frame, no frame backlog.
function createGameRuntime(onFrame,maxFps=25){
 let id=0,running=false,next=0,since=0,frames=0,fps=0;const period=maxFps>0?1000/maxFps:0;
 function frame(){id=0;if(!running)return;const now=Date.now();if(now>=next){const target=next+period;next=target>now?target:now+period;frames++;if(now-since>=1000){fps=Math.round(frames*1000/(now-since));frames=0;since=now;}onFrame();}if(running&&!id)id=Ouse.timers.requestFrame(frame);}
 return {start(){if(running)return;running=true;next=since=Date.now();frames=fps=0;id=Ouse.timers.requestFrame(frame);},stop(){running=false;if(id)Ouse.timers.clear(id);id=0;fps=0;},get running(){return running;},get fps(){return fps;}};
}

// Deterministic platform physics, independent of LVGL and the JS runtime host.
// Coordinates use a 16 px tile grid; y is the character's feet position.
function createPixelAdventureEngine(definitions,tileBytes) {
    const TILE=16, HALF=6, BODY=24, DT=1/60;
    const specs=definitions;
    let state, direction=0, buffered=0, accumulator=0;
    function load(index,saved=null) {
        index=Math.max(0,Math.min(2,index|0));
        const spec=specs[index],tiles=tileBytes.subarray(spec.tileOffset,spec.tileOffset+spec.columns*spec.rows);
        const fruits=spec.fruits.map(([x,y])=>({x,y,taken:false}));
        state={level:index,name:spec.name,skin:spec.skin,width:spec.columns*16,height:spec.rows*16,
            revision:spec.revision,columns:spec.columns,tiles,fruits,spikes:spec.spikes.map(([x,y])=>({x,y})),
            checkpoints:spec.checkpoints,exit:{x:spec.exit[0],y:spec.exit[1]},
            hero:{x:spec.spawn[0],y:spec.spawn[1],vx:0,vy:0,grounded:true,coyote:.1,jumps:0,face:1,doubleTime:0},
            checkpoint:false,checkpointId:null,respawn:{x:spec.spawn[0],y:spec.spawn[1]},
            deaths:0,collected:0,elapsed:0,phase:'ready',hitTime:0,room:0,steps:0,jumpSerial:0,restored:false};
        if(saved&&saved.revision===spec.revision&&saved.level===index){
            const flag=spec.checkpoints.find(c=>c.id===saved.checkpoint);
            if(flag){
                state.checkpoint=true;state.checkpointId=flag.id;state.respawn={x:flag.x,y:flag.y};
                state.hero.x=flag.x;state.hero.y=flag.y;state.restored=true;
                if(Array.isArray(saved.taken))for(const id of saved.taken)if(Number.isInteger(id)&&id>=0&&id<fruits.length)fruits[id].taken=true;
                state.collected=fruits.filter(f=>f.taken).length;
                state.elapsed=Number.isFinite(saved.elapsed)?Math.max(0,Math.min(86400,saved.elapsed)):0;
                state.deaths=Number.isInteger(saved.deaths)?Math.max(0,Math.min(1000000,saved.deaths)):0;
            }
        }
        state.room=Math.floor(state.hero.x/320);
        clearInput();accumulator=0;return state;
    }
    function serialize(){
        if(!state.checkpointId||state.phase==='clear')return null;
        return {revision:state.revision,level:state.level,checkpoint:state.checkpointId,
            taken:state.fruits.flatMap((f,i)=>f.taken?[i]:[]),elapsed:Math.round(state.elapsed),deaths:state.deaths};
    }
    function tile(x,y){return x<0||x>=state.width/TILE?1:y<0||y>=state.height/TILE?0:state.tiles[y*state.columns+x];}
    function clearInput(){direction=0;buffered=0;}
    function move(value){direction=Math.max(-1,Math.min(1,value|0));}
    function jump(){if(state.phase==='playing')buffered=.12;}
    function play(){if(state.phase==='ready'||state.phase==='paused')state.phase='playing';}
    function pause(){if(state.phase==='playing')state.phase='paused';clearInput();accumulator=0;}
    function die(){
        if(state.phase!=='playing')return;
        state.deaths++;state.phase='hurt';state.hitTime=.45;clearInput();
    }
    function respawn(){
        Object.assign(state.hero,{x:state.respawn.x,y:state.respawn.y,vx:0,vy:0,grounded:true,coyote:.1,jumps:0,doubleTime:0});
        state.room=Math.floor(state.hero.x/320);state.phase='playing';clearInput();
    }
    function step() {
        if(state.phase==='hurt'){state.hitTime-=DT;if(state.hitTime<=0)respawn();return;}
        if(state.phase!=='playing')return;
        const h=state.hero;state.elapsed+=DT;state.steps++;
        h.coyote=Math.max(0,h.coyote-DT);h.doubleTime=Math.max(0,h.doubleTime-DT);
        if(buffered>0){
            if(h.grounded||h.coyote>0||h.jumps<2){
                // Stepping off a ledge consumes the ground jump after coyote time.
                h.jumps=h.grounded||h.coyote>0?1:Math.max(1,h.jumps)+1;
                h.vy=h.jumps===1?-245:-225;h.grounded=false;h.coyote=0;
                state.jumpSerial++;
                if(h.jumps===2)h.doubleTime=.2;
                buffered=0;
            } else buffered=Math.max(0,buffered-DT);
        }
        h.vx=direction*90;if(direction)h.face=direction;
        h.x+=h.vx*DT;
        const left=Math.floor((h.x-HALF)/TILE),right=Math.floor((h.x+HALF-.001)/TILE);
        for(let y=Math.floor((h.y-BODY+.01)/TILE);y<=Math.floor((h.y-.01)/TILE);y++){
            if(h.vx>0&&tile(right,y)===1){h.x=right*TILE-HALF;h.vx=0;}
            if(h.vx<0&&tile(left,y)===1){h.x=(left+1)*TILE+HALF;h.vx=0;}
        }
        const oldY=h.y;h.vy=Math.min(320,h.vy+800*DT);h.y+=h.vy*DT;h.grounded=false;
        for(let x=Math.floor((h.x-HALF+.01)/TILE);x<=Math.floor((h.x+HALF-.01)/TILE);x++){
            if(h.vy>=0){
                for(let row=Math.floor((oldY-.001)/TILE);row<=Math.floor(h.y/TILE);row++){
                    if(tile(x,row)&&oldY<=row*TILE+.001&&h.y>=row*TILE){h.y=row*TILE;h.vy=0;h.grounded=true;h.jumps=0;h.coyote=.1;break;}
                }
            }else{
                const row=Math.floor((h.y-BODY)/TILE);
                if(tile(x,row)===1){h.y=(row+1)*TILE+BODY;h.vy=0;}
            }
        }
        if(!h.grounded&&h.coyote<=0&&h.jumps===0)h.jumps=1;
        h.x=Math.max(HALF,Math.min(state.width-HALF,h.x));
        if(h.y>state.height+32){die();return;}
        for(const p of state.spikes)if(h.x+HALF>p.x+3&&h.x-HALF<p.x+13&&h.y>p.y-6&&h.y-BODY<p.y){die();return;}
        for(const f of state.fruits)if(!f.taken&&Math.abs(h.x-f.x)<13&&h.y>f.y-8&&h.y-BODY<f.y+8){f.taken=true;state.collected++;}
        for(const flag of state.checkpoints)if(state.checkpointId!==flag.id&&Math.abs(h.x-flag.x)<18&&h.y>=flag.y-40&&h.y<=flag.y+6){
            state.checkpoint=true;state.checkpointId=flag.id;state.respawn={x:flag.x,y:flag.y};
        }
        state.room=Math.floor(h.x/320);
        if(Math.abs(h.x-state.exit.x)<16&&Math.abs(h.y-state.exit.y)<18&&h.grounded){state.phase='clear';clearInput();}
    }
    function advance(seconds){
        if(!Number.isFinite(seconds)||seconds<0)return;
        if(state.phase!=='playing'&&state.phase!=='hurt'){accumulator=0;return;}
        accumulator+=Math.min(.125,seconds);let n=0;
        while(accumulator>=DT&&n++<8){step();accumulator-=DT;}
    }
    load(0);
    return {load,move,jump,play,pause,clearInput,advance,serialize,get state(){return state;},specs};
}

// Native playback/mixing owns PCM work. Only control messages run in the game loop.
function createManorAudio(host){
 const clips=JSON.parse(host.assets.readText('audio'));
 let wanted=false,closed=false,nextCheck=0,retryAt=0,nextReport=1000,effects=0;
 function update(now){
  if(closed||!wanted||now<nextCheck)return;nextCheck=now+250;
  const state=host.media.get();
  if(state&&state.active){
   if(state.paused)host.media.resume();
   if(now>=nextReport){nextReport=now+10000;console.log('MANOR_AUDIO '+JSON.stringify({positionMs:state.positionMs||0,effects,error:state.errorCode||0}));}
   return;
  }
  if(now<retryAt)return;retryAt=now+2000;
  if(host.media.play({asset:'music',loop:true}))host.media.setInfo({title:'跳跳庄园',artist:'庄园小调'});
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

// Reuse licensed game atlases for living orchard art and themed stage cards.
function drawManorMenu(screen,w,h,time,selected,runs){
 const out=[],rect=(x,y,width,height,color,radius=0,opacity=255)=>out.push({op:'fillRect',x:Math.round(x),y:Math.round(y),width:Math.max(1,Math.round(width)),height:Math.max(1,Math.round(height)),color,radius,opacity});
 const image=(src,x,y,width,height,sx,sy,sw=32,sh=32)=>out.push({op:'drawImage',src,x:Math.round(x),y:Math.round(y),width:Math.round(width),height:Math.round(height),sourceX:sx,sourceY:sy,sourceWidth:sw,sourceHeight:sh});
 function actor(x,y,size){const f=Math.floor(time*8)%6;image('actors',x,y,size,size,f%8*32,Math.floor(f/8)*32);}
 function cloud(x,y,size){rect(x,y,size*2,size,0xffffff,size/2,160);rect(x+size*.45,y-size*.35,size,size,0xffffff,size/2,160);}
 rect(0,0,w,h,0xf8e8bb);rect(0,h*.48,w,h*.52,0xd4e7ba);
 for(let i=0;i<4;i++)cloud((i*151-time*5)%(w+150)-80,42+i%2*38,24+i%2*10);
 for(let i=-1;i<5;i++)rect(i*124-time*2%124,h*.55,160,h*.5,0xb5d3a5,70);
 if(screen==='home'){
  rect(0,h-38,w,38,0x57784f);rect(0,h-38,w,8,0x8fbc68);
  const ground=h-38,treeX=Math.round(w*.24),treeY=ground-108;
  rect(treeX-5,treeY+24,12,88,0x99663f,3);rect(treeX-55,treeY-14,112,66,0x7da75a,28);rect(treeX-33,treeY-42,76,68,0x91bb68,28);
  for(let i=0;i<5;i++)image('objects',treeX-53+i*23,treeY-18+(i%2)*24+Math.sin(time*2+i)*3,24,24,Math.floor(time*9+i)%4*32,32+(i%3)*32);
  actor(treeX-40,ground-88-Math.abs(Math.sin(time*1.7))*7,88);
  for(let x=0;x<w;x+=64)image('objects',x,ground,64,20,0,0,16,16);
  image('objects',w-58,h-90,40,40,192,32);
 }else{
  const gap=12,cw=(w-48)/3,ch=h-84,y=62;
  for(let i=0;i<3;i++){
   const x=12+i*(cw+gap),sky=[0xc8e8cd,0xf4d9b4,0xcdd4ef][i],grass=[0x83ad60,0xc59a57,0x8b9fc0][i];
   rect(x,y,cw,ch,0xfffbec,16);rect(x+6,y+6,cw-12,ch-68,sky,12);
   const base=y+ch-70;
   for(let j=0;j<3;j++){
    const tx=x+10+j*(cw-20)/3,ty=base-(i===1?j*13:i===2?(j%2)*30:0),tw=(cw-22)/3;
    rect(tx,ty,tw,i===2?12:base-ty+18,grass,2);image('objects',tx,ty-5,tw,10,i*48,0,16,16);
   }
   actor(x+18,base-46-(i===2?10:0)+Math.sin(time*2+i)*2,44);
   image('objects',x+cw-44,base-42-(i?24:0),32,32,192,32);
   if(i===2)image('objects',x+cw/2-8,base-15,16,16,144,0,16,16);
   image('objects',x+cw/2-12,y+22+Math.sin(time*2+i)*4,24,24,Math.floor(time*8)%4*32,32+i*32);
   if(i===selected)out.push({op:'strokeRect',x:Math.round(x),y, width:Math.round(cw),height:ch,radius:16,lineWidth:3,color:0xd7a34e});
   if(runs[i])image('objects',x+cw-30,y+8,22,22,192,32);
  }
 }
 return out;
}

// UI and rendering adapter; engine.js owns deterministic world physics.
const game=createPixelAdventureEngine(JSON.parse(Ouse.assets.readText('levels')),new Uint8Array(Ouse.assets.readBytes('tiles'))),animations=JSON.parse(Ouse.assets.readText('animations'));
const sound=createManorAudio(Ouse);
const loop=createGameRuntime(tick);
let width=480,height=320,scale=2,viewWidth=240,cameraX=0,cameraY=0;
let timer=0,last=0,closed=false,active=true,renderClock=0,dirty=true,screen='home';
let leftHeld=false,rightHeld=false,jumpHeld=false,hudKey='',selectedLevel=0;
let progress={unlocked:1,best:[0,0,0],runs:[null,null,null]},saveBusy=false,saveAgain=false,saveError=false,storageReady=false,pendingStart=null;
let submitted=0,maxCommands=0;
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
function stopInput(){leftHeld=rightHeld=jumpHeld=false;game.clearInput();hudKey='';}
function updateDirection(){game.move((rightHeld?1:0)-(leftHeld?1:0));hudKey='';}
function persist(){
 saveAgain=true;if(saveBusy||closed)return;saveAgain=false;saveBusy=true;
 Ouse.storage.set('pixel-adventure-v1',{schema:2,unlocked:progress.unlocked,best:progress.best.slice(),runs:progress.runs.slice(),selectedLevel})
 .then(()=>{saveBusy=false;saveError=false;if(saveAgain)persist();})
 .catch(()=>{saveBusy=false;saveError=true;hudKey='';});
}
function finish(){const s=game.state;progress.unlocked=Math.max(progress.unlocked,Math.min(3,s.level+2));progress.best[s.level]=Math.max(progress.best[s.level],s.collected);progress.runs[s.level]=null;persist();screen='result';stopInput();}
function followCamera(seconds,snap=false){
 const h=game.state.hero,target=clamp(h.x-viewWidth*.38,0,Math.max(0,game.state.width-viewWidth)),targetY=clamp(h.y-100,0,Math.max(0,game.state.height-160));
 cameraX=snap?target:cameraX+(target-cameraX)*(1-Math.exp(-10*Math.min(.125,seconds)));
 cameraY=snap?targetY:cameraY+(targetY-cameraY)*(1-Math.exp(-8*Math.min(.125,seconds)));
}
function rememberCheckpoint(){
 const run=game.serialize();if(!run)return;
 progress.runs[game.state.level]=run;persist();
}
function start(index,fresh=false){
 if(!storageReady){pendingStart={index,fresh};return;}
 selectedLevel=clamp(index|0,0,2);
 if(fresh){progress.runs[selectedLevel]=null;persist();}
 game.load(selectedLevel,fresh?null:progress.runs[selectedLevel]);game.play();screen='game';
 if(game.state.restored)console.log('ORCHARD_RESUME '+JSON.stringify({level:selectedLevel,flag:game.state.checkpointId,x:game.state.hero.x,y:game.state.hero.y}));
 stopInput();followCamera(0,true);dirty=true;syncScreens();startLoop();
}
function home(){rememberCheckpoint();game.pause();screen='home';stopInput();dirty=true;syncScreens();}
function chooseLevels(){rememberCheckpoint();game.pause();screen='levels';stopInput();dirty=true;syncScreens();}
function syncScreens(){
 if(!active||closed)return;
 Ouse.game.setEnabled(screen==='game'||screen==='pause');
 sound.sync(screen!=='pause');
 const playing=screen==='game',overlay=screen==='pause'||screen==='result';
 for(const [id,show] of [['home',screen==='home'],['levels',screen==='levels'],['modal',overlay],['left',playing],['right',playing],['jump',playing],['pause',playing]]){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
 Ouse.ui.setText('homePlay',progress.runs[selectedLevel]?'继续冒险':'开始冒险');
 for(let i=0;i<3;i++){Ouse.ui.setText('levelName'+i,game.specs[i].name);Ouse.ui.setText('levelDifficulty'+i,game.specs[i].difficulty);Ouse.ui.setEnabled('level'+i,true);}
 if(overlay){const s=game.state;
  Ouse.ui.setText('title',screen==='result'?(s.level===2?'冒险完成！':'关卡完成！'):'冒险暂停');
  Ouse.ui.setText('info',screen==='result'?'水果 '+s.collected+'/'+s.fruits.length+' · 重试 '+s.deaths+' 次\n用时 '+Math.round(s.elapsed)+' 秒':'左右移动 · 跳跃键可二段跳\n触碰旗帜可保存本关检查点');
  Ouse.ui.setText('play',screen==='result'?(s.level===2?'再玩一次':'下一关'):'继续冒险');
 }
 hudKey='';
}
function requestScreen(){const d=Ouse.display.get();if(d.orientation!=='landscape')Ouse.display.setOrientation('landscape');Ouse.display.setFullscreen(true);}
function layout(){
 const d=Ouse.display.get();if(d.orientation!=='landscape'){requestScreen();return;}
 width=Math.floor(d.contentWidthDp);height=Math.floor(d.contentHeightDp);scale=height/160;viewWidth=width/scale;
 const pw=Math.min(340,width-32),px=Math.round((width-pw)/2),mw=Math.min(320,width-32),mh=192;
 Ouse.ui.layout([
  {id:'world',x:0,y:0,width,height},{id:'hud',x:0,y:0,width,height},{id:'home',x:0,y:0,width,height},{id:'levels',x:0,y:0,width,height},
  {id:'homeTitle',x:26,y:20,width:260,height:48},
  {id:'homePlay',x:width-188,y:Math.round(height*.34),width:164,height:44},{id:'homeLevels',x:width-188,y:Math.round(height*.34)+54,width:164,height:42},{id:'homeExit',x:width-188,y:Math.round(height*.34)+106,width:164,height:38},
  {id:'levelsTitle',x:110,y:14,width:width-220,height:34},{id:'levelsBack',x:12,y:14,width:88,height:34},
  {id:'pause',x:width-64,y:14,width:48,height:38},{id:'left',x:24,y:height-88,width:76,height:64},{id:'right',x:112,y:height-88,width:76,height:64},{id:'jump',x:width-104,y:height-96,width:80,height:72},
  {id:'modal',x:Math.round((width-mw)/2),y:Math.round((height-mh)/2),width:mw,height:mh},{id:'title',x:12,y:8,width:mw-24,height:28},{id:'info',x:12,y:42,width:mw-24,height:40},
  {id:'play',x:12,y:92,width:Math.floor((mw-32)/2),height:38},{id:'retry',x:Math.ceil(mw/2)+4,y:92,width:Math.floor((mw-32)/2),height:38},{id:'toMenu',x:12,y:142,width:mw-24,height:36},
  ...[0,1,2].flatMap(i=>[{id:'level'+i,x:12+i*(Math.floor((width-48)/3)+12),y:62,width:Math.floor((width-48)/3),height:height-84},{id:'levelName'+i,x:6,y:height-144,width:Math.floor((width-48)/3)-12,height:30},{id:'levelDifficulty'+i,x:6,y:height-115,width:Math.floor((width-48)/3)-12,height:24}])
 ]);
 stopInput();followCamera(0,true);dirty=true;syncScreens();
}
function render(){
 if(screen==='home'||screen==='levels'){const out=drawManorMenu(screen,width,height,renderClock,selectedLevel,progress.runs);submitted++;maxCommands=Math.max(maxCommands,out.length);Ouse.canvas.submit('world',out);dirty=false;return;}
 const s=game.state,h=s.hero,out=[],bg=[0xb8d9c4,0xf0cfb5,0xc7bfdf],far=[0x97bba8,0xd8aa94,0xa89cbd];
 function rect(x,y,w,hh,color,radius=0,opacity=255){out.push({op:'fillRect',x:Math.round(x),y:Math.round(y),width:Math.max(1,Math.round(w)),height:Math.max(1,Math.round(hh)),color,radius,opacity});}
 function image(src,x,y,w,hh,sx,sy,sw,sh){out.push({op:'drawImage',src,x:Math.round(x*scale),y:Math.round((y-cameraY)*scale),width:Math.max(1,Math.round(w*scale)),height:Math.max(1,Math.round(hh*scale)),sourceX:sx,sourceY:sy,sourceWidth:sw,sourceHeight:sh});}
 rect(0,0,width,height,bg[s.skin]);
 for(let n=-1;n<Math.ceil(viewWidth/76)+2;n++){const x=((n*76-cameraX*.18)%(viewWidth+152)+viewWidth+152)%(viewWidth+152)-76;rect(x*scale,(82-cameraY*.04)*scale,58*scale,100*scale,far[s.skin],16);}
 const first=Math.max(0,Math.floor(cameraX/16)),end=Math.min(s.width/16-1,Math.floor((cameraX+viewWidth)/16));
 const top=Math.max(0,Math.floor(cameraY/16)),bottom=Math.min(s.height/16-1,Math.floor((cameraY+160)/16));
 const tile=(row,col)=>row<0?0:s.tiles[row*s.columns+col];
 // Interior soil is merged into row spans; map size never increases per-frame tile work.
 for(let row=top;row<=bottom;row++)for(let col=first;col<=end;col++){
  const kind=tile(row,col);if(!kind)continue;
  const surface=kind===2||row===0||!tile(row-1,col);
  if(surface)image('objects',col*16-cameraX,row*16,16,16,s.skin*48,0,16,16);
  else{
   let lastCol=col;
   while(lastCol<end&&tile(row,lastCol+1)===1&&row>0&&tile(row-1,lastCol+1)===1)lastCol++;
   image('objects',col*16-cameraX,row*16,(lastCol-col+1)*16,16,s.skin*48+16,0,16,16);col=lastCol;
  }
 }
 const visible=(x,margin=32)=>x>=cameraX-margin&&x<=cameraX+viewWidth+margin;
 for(const spike of s.spikes)if(visible(spike.x)&&spike.y>cameraY-32&&spike.y<cameraY+192)image('objects',spike.x-cameraX,spike.y-16,16,16,144,0,16,16);
 for(let n=0;n<s.fruits.length;n++){const f=s.fruits[n];if(!f.taken&&visible(f.x))image('objects',f.x-cameraX-16,f.y-16+Math.round(Math.sin(renderClock*4+n)*2),32,32,Math.floor(renderClock*12)%4*32,32+s.skin*32,32,32);}
 for(const flag of s.checkpoints)if(visible(flag.x))image('objects',flag.x-cameraX-16,flag.y-32,32,32,s.checkpointId===flag.id?192:160,32,32,32);
 if(visible(s.exit.x))image('objects',s.exit.x-cameraX-16,s.exit.y-32,32,32,224,32,32,32);
 const name=s.phase==='hurt'?'Hit':h.doubleTime>0?'Double Jump':!h.grounded?(h.vy<0?'Jump':'Fall'):h.vx?'Run':'Idle',a=animations[name],frame=a.first+Math.floor(renderClock*20)%a.count+(h.face<0?24:0);
 if(s.phase!=='hurt'||Math.floor(s.hitTime*20)%2===0)image('actors',h.x-cameraX-16,h.y-32,32,32,frame%8*32,Math.floor(frame/8)*32,32,32);
 if(screen==='home'||screen==='levels')rect(0,0,width,height,0x122d2a,0,210);
 if(screen==='pause'||screen==='result')rect(0,0,width,height,0x122d2a,0,120);
 if(out.length>256)throw Error('Pixel Adventure scene budget exceeded');maxCommands=Math.max(maxCommands,out.length);submitted++;Ouse.canvas.submit('world',out);dirty=false;
}
function renderHud(){
 const s=game.state,playing=screen==='game',key=[loop.fps,width,height,s.level,s.collected,s.deaths,s.checkpointId,screen,leftHeld,rightHeld,jumpHeld,saveError].join(':');if(key===hudKey)return;hudKey=key;
 const out=[];
 if(playing){
  out.push({op:'drawText',x:20,y:44,width:90,height:24,text:(loop.fps?loop.fps+' FPS':'-- FPS'),color:0x213b35});
  out.push({op:'drawText',x:20,y:14,width:width-100,height:28,text:'第 '+(s.level+1)+' 关   水果 '+s.collected+'/'+s.fruits.length+(s.checkpoint?'   旗 '+(s.checkpoints.findIndex(c=>c.id===s.checkpointId)+1):'')+(saveError?'   存档失败':''),color:0x213b35});
  for(const [id,held] of [['left',leftHeld],['right',rightHeld],['jump',jumpHeld],['pause',false]]){
   const x=id==='left'?24:id==='right'?112:id==='jump'?width-104:width-64,y=id==='pause'?14:id==='jump'?height-96:height-88,w=id==='jump'?80:id==='pause'?48:76,hh=id==='pause'?38:id==='jump'?72:64;
   if(held)out.push({op:'fillRect',x,y,width:w,height:hh,radius:18,color:0xffffff,opacity:45});
   out.push({op:'strokeRect',x,y,width:w,height:hh,radius:18,lineWidth:2,color:0xffffff,opacity:170});
   if(id==='left'||id==='right'){const cx=x+w/2,cy=y+hh/2,sign=id==='left'?-1:1;out.push({op:'fillTriangle',x:Math.round(cx+sign*12),y:Math.round(cy),x2:Math.round(cx-sign*6),y2:Math.round(cy-12),x3:Math.round(cx-sign*6),y3:Math.round(cy+12),color:0xffffff,opacity:200});}
   else if(id==='jump'){
    const cx=Math.round(x+w/2),cy=Math.round(y+hh/2);
    out.push({op:'fillTriangle',x:cx,y:cy-17,x2:cx-14,y2:cy-2,x3:cx+14,y3:cy-2,color:0xffffff,opacity:220});
    out.push({op:'fillRect',x:cx-4,y:cy-3,width:8,height:21,radius:2,color:0xffffff,opacity:220});
   }else out.push({op:'drawText',x:x+8,y:y+Math.round((hh-26)/2),width:w-16,height:26,text:'暂停',color:0xffffff,opacity:220});
  }
 }
 Ouse.canvas.submit('hud',out);
}
function tick(){
 if(closed||!active)return;const now=Date.now(),seconds=Math.max(0,(now-last)/1000);last=now;const previous=game.state.phase,previousFlag=game.state.checkpointId,previousFruit=game.state.collected,previousJump=game.state.jumpSerial;
 sound.update(now);
 if(screen==='game'){
  game.advance(seconds);if(game.state.checkpointId!==previousFlag){rememberCheckpoint();console.log('ORCHARD_FLAG '+JSON.stringify({level:game.state.level,flag:game.state.checkpointId,x:game.state.respawn.x,y:game.state.respawn.y}));}if(previous==='hurt'&&game.state.phase==='playing')console.log('ORCHARD_RESPAWN '+JSON.stringify({level:game.state.level,flag:game.state.checkpointId,x:game.state.hero.x,y:game.state.hero.y}));renderClock+=Math.min(.125,seconds);followCamera(seconds,previous==='hurt'&&game.state.phase==='playing');dirty=true;
  if(game.state.jumpSerial!==previousJump)sound.effect(game.state.hero.jumps===2?'double':'jump');
  if(game.state.collected>previousFruit)sound.effect('fruit');
  if(game.state.checkpointId!==previousFlag)sound.effect('flag');
  if(game.state.phase==='clear'){sound.effect('clear');finish();syncScreens();}else if(previous!==game.state.phase&&game.state.phase==='hurt'){sound.effect('hurt');stopInput();}
 }
 else if(screen==='home'||screen==='levels'){renderClock+=Math.min(.125,seconds);dirty=true;}
 if(dirty)render();renderHud();
}
function startLoop(){if(active&&!closed){last=Date.now();loop.start();}}
function stopLoop(){loop.stop();stopInput();}
for(const id of ['left','right']){
 Ouse.ui.on(id,'pointerdown',()=>{if(screen!=='game'||game.state.phase!=='playing')return;if(id==='left')leftHeld=true;else rightHeld=true;updateDirection();});
 for(const event of ['pointerup','pointercancel'])Ouse.ui.on(id,event,()=>{if(id==='left')leftHeld=false;else rightHeld=false;updateDirection();});
}
Ouse.ui.on('jump','pointerdown',()=>{if(screen!=='game')return;jumpHeld=true;game.jump();hudKey='';});
for(const event of ['pointerup','pointercancel'])Ouse.ui.on('jump',event,()=>{jumpHeld=false;hudKey='';});
Ouse.ui.on('pause','click',()=>{if(screen!=='game')return;game.pause();screen='pause';stopInput();dirty=true;syncScreens();});
Ouse.ui.on('homePlay','click',()=>start(selectedLevel));Ouse.ui.on('homeLevels','click',chooseLevels);Ouse.ui.on('levelsBack','click',home);Ouse.ui.on('homeExit','click',()=>Ouse.home());
Ouse.ui.on('play','click',()=>{if(screen==='result')start((game.state.level+1)%3);else{game.play();screen='game';stopInput();dirty=true;syncScreens();}});
Ouse.ui.on('retry','click',()=>start(game.state.level,true));Ouse.ui.on('toMenu','click',home);
for(let i=0;i<3;i++)Ouse.ui.on('level'+i,'click',()=>start(i));
Ouse.display.onChange(layout);
globalThis.app={onPause(){rememberCheckpoint();game.pause();if(screen==='game')screen='pause';sound.sync(false);active=false;stopLoop();},onResume(){active=true;requestScreen();layout();dirty=true;startLoop();},onClose(){sound.close();closed=true;active=false;stopLoop();}};
function storageLoaded(value){
 if(closed)return;
 if(value&&Number.isInteger(value.unlocked))progress.unlocked=clamp(value.unlocked,1,3);
 if(value&&Array.isArray(value.best))for(let i=0;i<3;i++)if(Number.isInteger(value.best[i]))progress.best[i]=clamp(value.best[i],0,game.specs[i].fruits.length);
 if(value&&value.schema===2&&Array.isArray(value.runs)){
  for(let i=0;i<3;i++){
   const saved=value.runs[i],spec=game.specs[i];
   if(saved&&saved.revision===spec.revision&&saved.level===i&&spec.checkpoints.some(c=>c.id===saved.checkpoint))progress.runs[i]=saved;
  }
  if(Number.isInteger(value.selectedLevel))selectedLevel=clamp(value.selectedLevel,0,2);
 }
 storageReady=true;
 if(pendingStart){const pending=pendingStart;pendingStart=null;start(pending.index,pending.fresh);}
 else if(active)syncScreens();
}
Ouse.storage.get('pixel-adventure-v1').then(storageLoaded,()=>storageLoaded(null));
requestScreen();layout();render();renderHud();startLoop();
