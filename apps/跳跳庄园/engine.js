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
