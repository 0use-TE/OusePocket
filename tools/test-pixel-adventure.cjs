// Verify real game code, authored routes, lifecycle and Canvas source rectangles.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),folder=path.join(root,'apps/跳跳庄园');
const source=name=>fs.readFileSync(path.join(folder,name),'utf8').replace(/\r\n/g,'\n');
assert.equal(source('app.js'),fs.readFileSync(path.join(root,'tools/app-shared/game-runtime.js'),'utf8')+'\n'+source('engine.js')+'\n'+source('audio.js')+'\n'+source('menu.js')+'\n'+source('game.js'),
    'rebuild app.js after changing source files');
const page=JSON.parse(fs.readFileSync(path.join(folder,'page.json')));
for(const [width,height] of [[320,416],[320,400],[480,256],[480,240]]){
    function bounds(node,pw,ph){
        const x=node.x||0,y=node.y||0,w=node.width==='fill'?pw:node.width,h=node.height==='fill'?ph:node.height;
        assert(x>=0&&y>=0&&x+w<=pw&&y+h<=ph,'cold-start bounds: '+node.id+' at '+width+'x'+height);
        for(const child of node.children||[])bounds(child,w,h);
    }
    bounds(page.root,width,height);
}
const engineContext=vm.createContext({console});
vm.runInContext(fs.readFileSync(path.join(folder,'engine.js'),'utf8'),engineContext);
const create=vm.runInContext('createPixelAdventureEngine',engineContext);
const definitions=JSON.parse(fs.readFileSync(path.join(folder,'assets/levels.json')));
const tileBytes=new Uint8Array(fs.readFileSync(path.join(folder,'assets/tiles.bin')));
const engine=create(definitions,tileBytes);
function simulate(seconds){for(let i=0;i<seconds*60;i++)engine.advance(1/60);}
engine.play();engine.move(1);simulate(.6);assert(engine.state.hero.x>100 && engine.state.hero.grounded,'opening platform must not block walking');engine.move(0);simulate(.2);assert.equal(engine.state.hero.y,definitions[0].spawn[1]);
engine.load(0);engine.play();
engine.jump();engine.advance(1/60);assert(engine.state.hero.vy<0);assert.equal(engine.state.hero.jumps,1);
simulate(.2);engine.jump();engine.advance(1/60);assert.equal(engine.state.hero.jumps,2);
const vy=engine.state.hero.vy;engine.jump();engine.advance(1/60);assert(engine.state.hero.vy>vy,'no third jump');
simulate(1);assert(engine.state.hero.grounded);
engine.pause();const frozen=JSON.stringify(engine.state);engine.move(1);simulate(10);assert.equal(JSON.stringify(engine.state),frozen);
engine.load(0);engine.play();engine.move(-1);simulate(2);assert(engine.state.hero.x>=6,'left world boundary');
// Full maps, rather than repeated rooms: verify dimensions, route variety and input-only traversal.
assert.deepEqual(definitions.map(d=>d.columns*16),[3200,4800,6400]);
assert.deepEqual(definitions.map(d=>d.rows*16),[384,576,704]);
assert(definitions[2].terrain.some(t=>t[3]-t[2]===2),'sky islands have voids below');
assert(definitions[1].terrain.some(t=>t[2]===13),'middle map climbs high terraces');
const routes=[];
for(let level=0;level<3;level++){
 engine.load(level);engine.play();const actions=[];
 for(let n=0;n<16000&&engine.state.phase!=='clear';n++){
  const s=engine.state,h=s.hero;
  const at=(x,y)=>{const col=Math.floor(x/16),row=Math.floor(y/16);return col<0||col>=s.width/16?1:row<0||row>=s.height/16?0:s.tiles[row*s.columns+col];};
  const pit=!at(h.x+8,h.y+1),wall=at(h.x+8,h.y-12)===1;
  const spike=s.spikes.some(p=>p.x-h.x>-8&&p.x-h.x<22&&Math.abs(p.y-h.y)<12);
  if((h.grounded&&(pit||wall||spike))||(!h.grounded&&h.jumps===1&&h.vy>=-40)){engine.jump();actions.push(n);}
  engine.move(1);engine.advance(1/60);
  assert.equal(s.deaths,0,'route died in level '+level+' at '+h.x+','+h.y);
 }
 assert.equal(engine.state.phase,'clear','normal input route must reach exit');
 routes.push({level:level+1,jumps:actions,seconds:engine.state.elapsed,fruits:engine.state.collected});
}
console.log('PASS three distinct large maps: all complete routes reachable using normal movement and jumps');
for(let level=0;level<3;level++)for(const flag of definitions[level].checkpoints){
 engine.load(level);engine.play();Object.assign(engine.state.hero,{x:flag.x-20,y:flag.y});engine.move(1);simulate(.1);
 assert.equal(engine.state.checkpointId,flag.id);engine.move(0);
 engine.state.fruits[0].taken=true;engine.state.collected=engine.state.fruits.filter(f=>f.taken).length;
 const saved=JSON.parse(JSON.stringify(engine.serialize()));assert.equal(saved.checkpoint,flag.id);
 const restored=create(definitions,tileBytes);restored.load(level,{...saved,x:999999,y:-999999});
 assert(restored.state.restored);assert.equal(restored.state.hero.x,flag.x);assert.equal(restored.state.hero.y,flag.y);assert.equal(restored.state.collected,engine.state.collected);
 Object.assign(engine.state.hero,{y:engine.state.height+33});engine.advance(1/60);assert.equal(engine.state.phase,'hurt');simulate(.6);
 assert.equal(engine.state.phase,'playing');assert.equal(engine.state.hero.x,flag.x);assert.equal(engine.state.hero.y,flag.y);
 assert.equal(engine.state.fruits[0].taken,true);
 restored.load(level,{...saved,revision:0});assert(!restored.state.restored);
 restored.load(level,{...saved,checkpoint:'unknown'});assert(!restored.state.restored);
}
console.log('PASS all 23 flags: touch, death respawn, JSON persistence/reopen, fruit retention and invalid-save rejection');

let now=0,seq=0,displayHeight=320,orientation='landscape',onDisplay,fullscreen=false,homes=0;
let gameMode=false;
const handlers=new Map(),timers=new Map(),layouts=new Map(),frames=new Map(),texts=new Map(),hidden=new Set(),writes=[];
const manifest=JSON.parse(fs.readFileSync(path.join(folder,'manifest.json')));
const clips=JSON.parse(source('assets/audio.json')),effectNames=[],mediaCalls=[];
let mediaState=null;
const media={
    get:()=>mediaState,
    play(options){assert.equal(options.asset,'music');assert.equal(options.loop,true);mediaCalls.push('play');mediaState={active:true,paused:false};return 1;},
    pause(){mediaCalls.push('pause');if(mediaState)mediaState.paused=true;},
    resume(){mediaCalls.push('resume');if(mediaState)mediaState.paused=false;},
    stop(){mediaCalls.push('stop');mediaState=null;},setInfo(){},
    effect(options){assert(mediaState&&mediaState.active&&!mediaState.paused);assert.equal(options.asset,'sounds');const name=Object.keys(clips).find(n=>clips[n].offset===options.offset&&clips[n].length===options.length);assert(name);effectNames.push(name);return true;}
};
const music=fs.readFileSync(path.join(folder,'assets/music.wav')),bank=fs.readFileSync(path.join(folder,'assets/sounds.pcm'));
assert.equal(music.toString('ascii',0,4),'RIFF');assert.equal(music.readUInt32LE(24),8000);assert.equal(music.readUInt16LE(22),1);
for(const clip of Object.values(clips)){assert.equal(clip.offset%2,0);assert.equal(clip.length%2,0);assert(clip.length>0&&clip.length<=64000&&clip.offset+clip.length<=bank.length);assert(bank.subarray(clip.offset,clip.offset+clip.length).some(b=>b));}
// A busy codec must not cause a play request every frame; app lifecycle owns playback.
{
    let clock=0,attempts=0,stops=0,pauses=0,resumes=0,state=null;
    const sandbox=vm.createContext({console,Date:{now:()=>clock}});
    vm.runInContext(source('audio.js'),sandbox);
    const audio=vm.runInContext('createManorAudio',sandbox)({assets:{readText:()=>JSON.stringify(clips)},media:{
        get:()=>state,play(){attempts++;return false;},setInfo(){},pause(){pauses++;},resume(){resumes++;state.paused=false;},stop(){stops++;},effect(){return true;}
    }});
    audio.sync(true);for(clock=50;clock<=1950;clock+=50)audio.update(clock);assert.equal(attempts,1);
    clock=2000;audio.update(clock);assert.equal(attempts,2);
    state={active:true,paused:false};audio.sync(false);assert.equal(pauses,1);assert.equal(audio.effect('jump'),false);
    state.paused=true;audio.sync(true);assert.equal(resumes,1);assert.equal(attempts,2,'resume keeps existing looping session');
    audio.close();audio.close();audio.sync(true);audio.update(99999);assert.equal(stops,1);assert.equal(attempts,2);assert.equal(audio.effect('fruit'),false);
}
const imageSizes=new Map(manifest.assets.filter(a=>a.type==='image').map(a=>{const b=fs.readFileSync(path.join(folder,a.path));return [a.id,[b.readUInt16LE(4),b.readUInt16LE(6)]];}));
const ctx=vm.createContext({console,Date:{now:()=>now},Ouse:{
    game:{setEnabled(value){gameMode=value;return true;}},
    media,
    assets:{readBytes(id){const asset=manifest.assets.find(a=>a.id===id);const b=fs.readFileSync(path.join(folder,asset.path));return b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength);},readText(id){return fs.readFileSync(path.join(folder,manifest.assets.find(a=>a.id===id).path),'utf8');}},
    canvas:{submit(id,commands){assert(commands.length<=256);for(const c of commands){for(const key of ['x','y','width','height','sourceX','sourceY'])if(key in c)assert(Number.isInteger(c[key]));if(c.op==='drawImage'){const [w,h]=imageSizes.get(c.src);assert(c.sourceX>=0&&c.sourceY>=0&&c.sourceX+c.sourceWidth<=w&&c.sourceY+c.sourceHeight<=h);}}frames.set(id,JSON.parse(JSON.stringify(commands)));}},
    display:{get:()=>({orientation,contentWidthDp:480,contentHeightDp:displayHeight}),setFullscreen(value){fullscreen=value;},setOrientation(value){orientation=value;},onChange(fn){onDisplay=fn;}},home(){homes++;},
    ui:{on(id,event,fn){handlers.set(id+':'+event,fn);},layout(items){items.forEach(f=>layouts.set(f.id,f));},setText(id,text){texts.set(id,text);},show(id){hidden.delete(id);},hide(id){hidden.add(id);},setEnabled(){}},
    timers:{requestFrame(fn){timers.set(++seq,fn);return seq;},clear(id){timers.delete(id);}},
    storage:{get:()=>Promise.resolve({unlocked:1,best:[0,0,0]}),set(key,value){writes.push([key,JSON.parse(JSON.stringify(value))]);return Promise.resolve();}}
}});
const run=code=>vm.runInContext(code,ctx);
function tick(n=1,ms=50){for(let i=0;i<n;i++){now+=ms;for(const [id,fn] of [...timers]){timers.delete(id);fn();}}}
async function main(){
    run(fs.readFileSync(path.join(folder,'app.js'),'utf8'));await Promise.resolve();
    assert.equal(mediaCalls[0],'play');
    assert(fullscreen);assert.equal(timers.size,1);assert.equal(run('screen'),'home');
    assert.equal(gameMode,false);
    assert(!hidden.has('home')&&hidden.has('left')&&hidden.has('modal'));
    handlers.get('homeLevels:click')();assert.equal(run('screen'),'levels');assert(!hidden.has('levels'));
    handlers.get('level2:click')();assert.equal(run('game.state.level'),2,'all levels directly selectable');
    handlers.get('pause:click')();assert.equal(run('screen'),'pause');handlers.get('toMenu:click')();assert.equal(run('screen'),'home');
    handlers.get('homeExit:click')();assert.equal(homes,1);
    handlers.get('level0:click')();assert.equal(run('screen'),'game');
    assert.equal(gameMode,true);
    const fastFrames=run('submitted');tick(60,16);const produced=run('submitted')-fastFrames;assert(produced>=23&&produced<=25,'shared scheduler caps at 25 FPS without drift');assert.equal(timers.size,1,'only one frame request pending');
    tick(75,16);assert(run('loop.fps')>=24&&run('loop.fps')<=25,'FPS measures actual game updates');
    tick(22,100);assert.equal(run('loop.fps'),10,'FPS falls when callbacks are slow');
    assert(frames.get('hud').some(c=>c.text==='10 FPS'),'measured FPS is visible');
    for(const h of [240,256,320]){
        displayHeight=h;onDisplay();
        for(const id of ['left','right','jump','pause','modal','homePlay','homeLevels','homeExit','levelsBack','level0','level1','level2']){
            const f=layouts.get(id);assert(f.x>=0&&f.y>=0&&f.x+f.width<=480&&f.y+f.height<=h,id);
        }
        assert.equal(run('scale*160'),h,'world fills full height');assert.equal(run('viewWidth*scale'),480,'world fills full width');
    }
    handlers.get('right:pointerdown')();tick(8);assert(run('game.state.hero.x')>80);
    handlers.get('jump:pointerdown')();tick(2);assert(run('game.state.hero.y')<definitions[0].spawn[1]);
    handlers.get('jump:pointerup')();handlers.get('jump:pointerdown')();tick();assert.equal(run('game.state.hero.jumps'),2);
    handlers.get('right:pointercancel')();const x=run('game.state.hero.x');tick();assert.equal(run('game.state.hero.x'),x);
    assert(effectNames.includes('jump')&&effectNames.includes('double'),'accepted physics jumps trigger different sounds');
    run('game.state.fruits[0].x=game.state.hero.x;game.state.fruits[0].y=game.state.hero.y-10;game.state.fruits[0].taken=false');tick();assert(effectNames.includes('fruit'));
    run('game.state.hero.y=game.state.height+40');tick();assert(effectNames.includes('hurt'));
    const hud=frames.get('hud');assert(hud.some(c=>c.op==='strokeRect'));assert(hud.filter(c=>c.op==='fillRect'&&c.width>=48&&c.height>=38).every(c=>c.opacity<100),'controls have no opaque backing');
    for(let level=0;level<3;level++)for(const x of [48,319,321,560,928]){
        run('game.load('+level+');game.state.hero.x='+x+';followCamera(0,true);render();hudKey="";renderHud()');assert(run('maxCommands')<=256);
    }
    for(let level=0;level<3;level++)for(const point of [{x:48,y:definitions[level].spawn[1]},...definitions[level].checkpoints,{x:definitions[level].exit[0],y:definitions[level].exit[1]}]){
        run('game.load('+level+');game.state.hero.x='+point.x+';game.state.hero.y='+point.y+';followCamera(0,true);render()');
        assert(run('maxCommands')<=256,'large maps retain bounded viewport rendering');
    }
    run('game.load(0);game.state.hero.x=318;followCamera(0,true)');const c0=run('cameraX');
    run('game.state.hero.x=322;followCamera(.05)');assert(run('cameraX')>c0&&run('cameraX')-c0<4,'camera must not jump by a room at x=320');
    run('game.state.hero.x=180;followCamera(.05)');assert(run('cameraX')<c0,'camera follows left movement');
    run('game.state.hero.x=game.state.width-6;followCamera(0,true)');assert.equal(run('cameraX'),run('game.state.width-viewWidth'));
    run('game.state.hero.x=6;followCamera(0,true)');assert.equal(run('cameraX'),0);
    handlers.get('level0:click')();handlers.get('right:pointerdown')();ctx.app.onPause();assert.equal(timers.size,0);assert.equal(run('screen'),'pause');
    assert(mediaState.paused,'background pauses audio');
    now+=60000;ctx.app.onResume();assert.equal(timers.size,1);const before=run('game.state.hero.x');tick();assert.equal(run('game.state.hero.x'),before);assert(!hidden.has('modal'));
    assert(mediaState.paused,'foreground pause menu stays silent');
    handlers.get('play:click')();run('game.state.fruits.forEach(f=>f.taken=true);game.state.collected=game.state.fruits.length;game.state.hero.x=game.state.exit.x;game.state.hero.y=game.state.exit.y');tick();
    assert.equal(run('screen'),'result');assert.equal(run('progress.unlocked'),2);assert.equal(writes.at(-1)[1].best[0],definitions[0].fruits.length);
    assert(effectNames.includes('clear'));assert(!mediaState.paused);
    handlers.get('play:click')();assert.equal(run('game.state.level'),1);assert.equal(run('screen'),'game');
    run('game.state.phase="hurt";game.state.hitTime=.2');ctx.app.onPause();ctx.app.onResume();handlers.get('play:click')();tick(6);assert.equal(run('game.state.phase'),'playing');
    run('start(0,true)');await Promise.resolve();await Promise.resolve();
    const flag=definitions[0].checkpoints[0];
    run('game.state.hero.x='+ (flag.x-20)+';game.state.hero.y='+flag.y+';');
    handlers.get('right:pointerdown')();tick(3);handlers.get('right:pointerup')();
    await Promise.resolve();await Promise.resolve();
    const payload=JSON.parse(JSON.stringify(writes.at(-1)[1]));assert.equal(payload.runs[0].checkpoint,flag.id);
    assert(effectNames.includes('flag'));
    assert(JSON.stringify(payload).length<4096);
    ctx.app.onClose();assert.equal(timers.size,0);
    assert.equal(mediaState,null,'closing game releases audio');
    ctx.Ouse.storage.get=()=>Promise.resolve(payload);
    const reopened=vm.createContext({console,Date:{now:()=>now},Ouse:ctx.Ouse});
    vm.runInContext(source('app.js'),reopened);await Promise.resolve();
    assert.equal(texts.get('homePlay'),'继续冒险');handlers.get('homePlay:click')();
    assert.equal(vm.runInContext('game.state.hero.x',reopened),flag.x);
    assert.equal(vm.runInContext('game.state.hero.y',reopened),flag.y);
    assert.equal(vm.runInContext('game.state.checkpointId',reopened),flag.id);
    reopened.app.onClose();assert.equal(timers.size,0);
    console.log('PASS flag activation persists through runtime storage; new app instance continues at the saved flag');
    ctx.app.onClose();assert.equal(timers.size,0);assert.equal(run('closed'),true);
    const out=path.join(root,'.tools/pixel-adventure');fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'routes.json'),JSON.stringify(routes,null,2));
    run('closed=false;active=true;start(0);renderClock=0;render();hudKey="";renderHud()');
    fs.writeFileSync(path.join(out,'preview-frame.json'),JSON.stringify({width:480,height:displayHeight,world:frames.get('world'),hud:frames.get('hud')}));
    console.log('PASS independent home/level selection, fullscreen scene, transparent controls, simultaneous move/jump, continuous camera, lifecycle and save');
    console.log('PASS embedded WAV/PCM, all six game sounds, background/pause/resume and close audio lifecycle');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
