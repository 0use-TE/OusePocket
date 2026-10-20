"use strict";
const backgroundHQ={
    sky:Ouse.assets.loadImage('sky-hq','Pictures/brave-road-sky-hq-v1.bin'),
    village:Ouse.assets.loadImage('village-hq','Pictures/brave-road-village-hq-v1.bin')
};
console.log('BRAVE_BG '+JSON.stringify(backgroundHQ));
// Source units are scaled by 1/2; numeric combat rules remain in original units.
const rules=JSON.parse(Ouse.assets.readText('rules'));
const current=JSON.parse(Ouse.assets.readText('first-level'));
const TILE=20,FPS=rules.fps,STEP=1/FPS,SCALE=.5,MAX_ENEMIES=12;
const worldW=current.bgSize[0]*TILE,worldH=current.bgSize[1]*TILE;
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const hudView=createGameHud('hud');
let screenW=480,screenH=256,fieldH=222,cameraX=0,cameraY=0,phase='menu';
let hero=null,enemies=[],items=[],words=[],wave=0,queue=[],lockArea=null;
let level=1,xp=0,kills=0,frame=0,combo=0,comboTime=0,attackChain=0,chainTime=0;
let equipment={sword:1,shield:1,dress:1},move=0,climb=0,jumpPending=false,pickup=false;
let pointer=null,target=null,timer=0,last=0,accumulator=0,hudTime=0,dirty=true,closed=false;
let savePending=false,saveAgain=false,soundOn=true,audioWanted='',audioStarted='',audioRetry=0,audioError='';
let lastSceneCommands=0,uiSubmits=0,sceneSubmits=0,statsFrames=0,effectsPlayed=0;
let userTouched=false,attackHeld=false,uiActive=true,skillFlash=0,loaded=false;
const skillNames=['重击','回旋','冲击','落地'],skillCosts=[10,20,20,60],skillLevels=[5,7,15,20];
let lastControls='';
function maxHp(){const r=rules.hero;return r.hpBase+level*r.hpPerLevel+equipment.dress*r.hpPerDress;}
function maxMp(){return rules.hero.mpBase+level*rules.hero.mpPerLevel;}
function heroAttack(){return rules.hero.swordBaseAttack+equipment.sword*rules.hero.swordAttackPerWeapon;}
function heroDefense(){return rules.hero.shieldBaseDefense+equipment.shield*rules.hero.shieldDefensePerItem;}
function damage(power,defense,multiple,random){return Math.max(1,Math.floor((power-defense)*multiple*(10+random)/10));}
function playEffect(name){
    if(!soundOn||phase!=='playing')return false;
    const clip=rules.sounds[name];if(!clip)return false;
    const accepted=Ouse.media.effect({asset:'sounds',offset:clip.offset,length:clip.length,gain:85});
    if(accepted)effectsPlayed++;return accepted;
}
function wantAudio(name){
    audioWanted=soundOn?name:'';audioRetry=0;
    const state=Ouse.media.get();
    if(state&&state.errorCode)audioStarted='';
    if(!audioWanted){if(state&&state.active)Ouse.media.pause();return;}
    if(audioStarted!==audioWanted&&state&&state.active)Ouse.media.stop();
}
function pumpAudio(ms){
    audioRetry-=ms;if(audioRetry>0)return;audioRetry=250;
    if(!soundOn||!audioWanted)return;
    const state=Ouse.media.get();
    if(state&&state.active){if(audioStarted===audioWanted&&state.paused)Ouse.media.resume();return;}
    if(state&&!state.active&&state.state==='ended'&&audioStarted===audioWanted&&audioWanted!=='music1')return;
    if(state&&state.errorCode&&audioStarted===audioWanted){audioError='音频不可用，请检查 SD 卡';return;}
    const id=Ouse.media.play({path:'Music/brave-road-'+audioWanted+'-v1.wav',loop:audioWanted==='music1'});
    if(id){audioStarted=audioWanted;Ouse.media.setInfo({title:'勇者之路 · '+(audioWanted==='music1'?'第一关':audioWanted==='win'?'胜利':'失败'),artist:'原游戏音频'});audioError='';}
}
function stopInput(){pointer=null;move=climb=0;target=null;jumpPending=false;pickup=false;attackHeld=false;}
function setPhase(next){phase=next;stopInput();dirty=true;hudView.invalidate();lastControls='';if(uiActive)syncControls();}
function cell(x,y){if(x<0||x>=worldW||y<0)return 2;if(y>=worldH)return 0;return current.bgCell[Math.floor(y/TILE)][Math.floor(x/TILE)];}
function moveBody(b,width,height){
    const oldX=b.x,oldY=b.y,nextX=oldX+b.vx;
    const edge=nextX+(b.vx>=0?width/2:-width/2);
    if(cell(edge,b.y-3)!==2&&cell(edge,b.y-height+3)!==2)b.x=clamp(nextX,TILE+width/2,worldW-TILE-width/2-1);
    else {b.vx=0;b.wall=true;}
    if(b.onGround&&cell(b.x,b.y+1)!==2&&cell(b.x,b.y-1)!==1)b.onGround=false;
    if(!b.onGround)b.vy=Math.min(15,b.vy+rules.hero.gravity*SCALE);
    let nextY=b.y+b.vy;b.onGround=false;
    if(b.vy>=0){
        for(let row=Math.max(0,Math.floor((oldY-1)/TILE));row<=Math.min(current.bgSize[1]-1,Math.floor(nextY/TILE));row++){
            let landed=false;
            for(const x of [b.x-width/2+1,b.x,b.x+width/2-1]){
                const c=cell(x,row*TILE+1),top=c===1?(row+1)*TILE:c===2?row*TILE:-1;
                if(top>=0&&oldY<=top+.01&&nextY>=top){nextY=top;b.vy=0;b.onGround=true;landed=true;break;}
            }
            if(landed)break;
        }
    }else if(cell(b.x,nextY-height)===2){nextY=Math.floor((nextY-height)/TILE+1)*TILE+height;b.vy=0;}
    b.y=nextY;
}
function resetStage(){
    hero={x:50,y:50,vx:0,vy:0,onGround:false,face:1,hp:maxHp(),mp:maxMp(),protect:20,
        action:'idle',actionFrame:0,actionLength:0,hitWindow:0,hitPower:1,hitRange:50,hitTop:-20,hitBottom:-5,hitX:0,hitY:0,jumps:0};
    enemies=[];items=[];words=[];queue=[];wave=0;lockArea=null;kills=0;frame=0;combo=comboTime=attackChain=chainTime=0;
    cameraX=cameraY=0;accumulator=0;dirty=true;audioError='';skillFlash=0;
}
function save(){
    saveAgain=true;if(savePending||closed)return;saveAgain=false;savePending=true;
    Ouse.storage.set('first-stage-v2',{version:2,level,xp,equipment,soundOn}).then(()=>{savePending=false;if(saveAgain)save();})
        .catch(()=>{savePending=false;audioError='存档未成功';});
}
function award(n){
    if(level<40)xp+=n;
    while(level<40&&xp>=50){xp-=50;level++;hero.hp=maxHp();hero.mp=maxMp();playEffect('lvup');words.push({x:hero.x,y:hero.y-40,text:'LEVEL UP',life:30,color:0xffdf70});}
    if(level===40)xp=Math.min(49,xp);save();
}
function menu(title,info){
    const was=phase;setPhase('menu');wantAudio('');
    Ouse.ui.setText('title',title||'勇者之路 · 第一关');
    Ouse.ui.setText('info',info||'左侧摇杆移动 / 上下爬梯与拾取\n右侧攻击、跳跃 / 右上四个技能');
    Ouse.ui.setText('play',was==='clear'||!hero||hero.hp<=0?'开始第一关':'继续冒险');
    Ouse.ui.setText('stage',soundOn?'声音：开':'声音：关');Ouse.ui.show('modal');
}
function startStage(){userTouched=true;resetStage();setPhase('playing');Ouse.ui.hide('modal');wantAudio('music1');last=Date.now();render();drawHud();}
function resume(){
    userTouched=true;
    if(!hero||hero.hp<=0||wave>=current.enemyArr.length&&kills===rules.stageEnemies){startStage();return;}
    setPhase('playing');Ouse.ui.hide('modal');wantAudio('music1');last=Date.now();
}
function endStage(won){
    setPhase(won?'clear':'dead');wantAudio(won?'win':'lose');save();
    Ouse.ui.setText('title',won?'第一关完成':'冒险结束');
    Ouse.ui.setText('info','击败 '+kills+'/28 · LV '+level+' · 经验 '+xp+'/50\n'+(won?'原版第一关波次已清空':'点击重新开始，再次挑战'));
    Ouse.ui.setText('play','重新挑战');Ouse.ui.show('modal');
}
function spawnEnemy(name,x,y){
    const r=rules.monsters[name];
    enemies.push({name,r,x,y,vx:x<cameraX?1:-1,vy:0,onGround:false,face:x<cameraX?1:-1,
        hp:r.thisAllHp,protect:4,stun:0,action:'walk',actionFrame:0,freeTime:50,target:false,dying:0,hitWindow:0});
}
function spawn(){
    if(!queue.length&&!lockArea&&wave<current.enemyArr.length&&cameraX>=current.enemyArr[wave][0]*TILE+5){
        const group=current.enemyArr[wave++];queue=group[3].slice();
        if(group[1]&&group[2]){lockArea={left:group[1]*TILE,right:group[2]*TILE};cameraX=lockArea.left;}
    }
    if(queue.length&&enemies.filter(e=>e.hp>0).length<12&&frame%10===0){
        spawnEnemy(queue.shift(),Math.random()<.5?cameraX-40:cameraX+520,cameraY+80);
    }
    if(!queue.length&&!enemies.some(e=>e.hp>0)){
        lockArea=null;
        if(wave===current.enemyArr.length&&kills===rules.stageEnemies){endStage(true);}
    }
}
function drop(e){
    const roll=Math.random();let name='';
    if(roll<.3)name=e.r.drops[Math.floor(Math.random()*e.r.drops.length)];
    else if(roll>.95&&e.r.rareDrops.length)name=e.r.rareDrops[Math.floor(Math.random()*e.r.rareDrops.length)];
    if(name&&items.length<28)items.push({name,x:e.x,y:e.y,vx:(Math.random()<.5?-1:1)*Math.floor(Math.random()*10)*SCALE,vy:-Math.floor(Math.random()*10+25)*SCALE,onGround:false,life:24*60});
}
function hitEnemy(e,multiple,hitX,hitY){
    if(e.hp<=0||e.protect)return false;
    const n=damage(heroAttack(),e.r.thisDef,multiple,Math.random());
    e.hp=Math.max(0,e.hp-n);e.protect=4;e.stun=15;e.target=true;e.action='hurt';e.actionFrame=0;
    e.vx=hero.face*hitX*SCALE;if(hitY<0){e.vy=hitY*SCALE;e.onGround=false;}
    words.push({x:e.x,y:e.y-40,text:String(n),life:18,color:0xfff1a4});
    playEffect('behit2');hero.mp=Math.min(maxMp(),hero.mp+1);combo++;comboTime=34;
    if(e.hp<=0){e.action='dead';e.actionFrame=0;e.dying=10;kills++;drop(e);award(e.r.thisExp+Math.floor(combo/5));}
    return true;
}
function heroHurt(e){
    if(hero.hp<=0||hero.protect)return;
    const n=damage(e.r.thisAtt,heroDefense(),1,Math.random());hero.hp=Math.max(0,hero.hp-n);hero.protect=20;
    hero.action='hurt';hero.actionFrame=0;hero.actionLength=5;hero.hitWindow=0;playEffect('behit1');
    words.push({x:hero.x,y:hero.y-45,text:String(n),life:18,color:0xff8970});
}
function beginAttack(special){
    if(hero.actionLength>0||!hero.onGround)return;
    if(special){
        if(level<5||hero.mp<10)return;hero.mp-=10;hero.action='special';hero.actionLength=20;
    }else{
        if(chainTime<=0)attackChain=0;
        hero.action=attackChain<2?'attack1':attackChain===2?'attack2':'attack3';
        hero.actionLength=hero.action==='attack1'?7:10;attackChain=(attackChain+1)%4;chainTime=20;
    }
    hero.actionFrame=0;hero.vx=0;hero.skillIndex=0;
}
function doSkill(index=0){
 if(phase!=='playing'||!hero||hero.hp<=0||level<skillLevels[index]||hero.mp<skillCosts[index]||hero.actionLength>0)return false;
 if(index<2&&!hero.onGround)return false;
 hero.mp-=skillCosts[index];hero.actionFrame=0;hero.hitWindow=0;hero.vx=0;hero.skillIndex=index;
 hero.action=['special','roll','charge','slamRise'][index];hero.actionLength=[20,28,15,8][index];
 if(index===1)hero.protect=Math.max(hero.protect,28);
 if(index===3){hero.vy=-1.5;hero.onGround=false;hero.protect=Math.max(hero.protect,8);}
 skillFlash=.15;dirty=true;drawHud();syncControls();return true;
}
function dealHeroAction(){
    if(hero.actionLength<=0)return;
    const f=hero.actionFrame;
    const active=hero.action==='special'?f===6:hero.action==='roll'?f===2:hero.action==='charge'?true:hero.action==='slamLand'?f===0:(hero.action==='attack1'||hero.action==='attack2'||hero.action==='attack3')&&f===2;
    if(active){
        hero.hitWindow=hero.action==='attack3'?5:hero.action==='special'?10:3;
        hero.hitPower=hero.action==='special'?3:hero.action==='attack3'?1.5:1;
        hero.hitRange=hero.action==='special'?90:hero.action==='attack3'?60:50;
        hero.hitTop=hero.action==='attack3'?-15:hero.action==='special'?-60:hero.action==='attack2'?-50:-20;
        hero.hitBottom=hero.action==='attack2'||hero.action==='special'?5:-5;
        hero.hitX=hero.action==='special'?20:hero.action==='attack3'?10:0;
        hero.hitY=hero.action==='special'?-20:hero.action==='attack3'||hero.action==='attack2'?-5:0;
        playEffect(hero.action==='special'||hero.action==='attack3'?'att3':hero.action==='attack2'?'att2':'att1');
    }
    if(active&&hero.skillIndex>0){
        hero.hitWindow=hero.action==='roll'?26:hero.action==='charge'?1:18;
        hero.hitPower=hero.action==='roll'?.5:hero.action==='charge'?1:3;
        hero.hitRange=hero.action==='charge'?90:hero.action==='roll'?90:110;
        hero.hitTop=hero.action==='slamLand'?-75:hero.action==='roll'?-35:-20;hero.hitBottom=-5;
        hero.hitX=hero.action==='charge'?5:10;hero.hitY=-5;
    }
    hero.actionFrame++;hero.actionLength--;
    if(hero.action==='slamRise'&&hero.actionLength===0){hero.action='slamFall';hero.actionLength=1000;hero.actionFrame=0;hero.vy=10;hero.protect=Math.max(hero.protect,1000);}
    if(hero.action==='slamFall'&&hero.onGround){hero.action='slamLand';hero.actionLength=20;hero.actionFrame=0;hero.protect=20;}

}
function collect(item){
    const n=item.name;
    if(n==='hps'||n==='hpm')hero.hp=Math.min(maxHp(),hero.hp+(n==='hps'?50:200));
    else if(n==='exps'||n==='expm')award(n==='exps'?20:100);
    else if(n==='s2')equipment.shield=Math.max(2,equipment.shield);
    else if(n==='d2'){const old=maxHp();equipment.dress=Math.max(2,equipment.dress);hero.hp=Math.min(maxHp(),hero.hp+maxHp()-old);}
    else if(n==='ws2')equipment.sword=Math.max(2,equipment.sword);
    else if(n==='wd2')equipment.dagger=2;
    item.life=0;playEffect('get1');save();
}
function step(){
    if(phase!=='playing')return;frame++;skillFlash=Math.max(0,skillFlash-STEP);
    if(hero.protect)hero.protect--;if(chainTime)chainTime--;if(comboTime)comboTime--;else combo=0;
    if(target!==null){move=Math.abs(target-hero.x)<4?0:target>hero.x?1:-1;if(!move)target=null;}
    if(move)hero.face=move;
    hero.vx=hero.action==='charge'&&hero.actionLength>0?hero.face*10:hero.actionLength>0?0:move*rules.hero.speed*SCALE;
    const ladder=[cell(hero.x,hero.y-5),cell(hero.x,hero.y-25)].some(c=>c===3||c===4);
    if(hero.onGround)hero.jumps=0;
    if(jumpPending){if(hero.actionLength<=0&&hero.jumps<(level>=10?2:1)&&!ladder){hero.vy=-15;hero.onGround=false;hero.jumps++;}jumpPending=false;}
    if(ladder&&climb){hero.y+=climb*1.5;hero.vy=0;hero.onGround=false;hero.action='climb';}
    else moveBody(hero,15,35);
    if(lockArea)hero.x=clamp(hero.x,lockArea.left+10,lockArea.right-10);
    cameraX=lockArea?clamp(cameraX,lockArea.left,Math.max(lockArea.left,lockArea.right-screenW)):clamp(hero.x-150,0,Math.max(0,worldW-screenW));
    cameraY=clamp(hero.y-fieldH*.7,0,Math.max(0,worldH-fieldH));
    if(attackHeld&&hero.actionLength<=0)beginAttack(false);
    dealHeroAction();
    if(hero.hitWindow>0){
        for(const e of enemies){
            const a=(e.x-hero.x)*hero.face,left=e.r.beHitLeft*SCALE,right=e.r.beHitRight*SCALE;
            if(a+right>=(hero.action==='roll'||hero.action==='slamLand'?-hero.hitRange:0)&&a+left<=hero.hitRange&&e.y+e.r.beHitBottom*SCALE>=hero.y+hero.hitTop&&e.y+e.r.beHitTop*SCALE<=hero.y+hero.hitBottom)hitEnemy(e,hero.hitPower,hero.hitX,hero.hitY);
        }
        hero.hitWindow--;
    }
    for(const e of enemies){
        if(e.hp<=0){e.dying--;e.actionFrame++;continue;}
        if(e.protect)e.protect--;if(e.stun)e.stun--;
        const dx=hero.x-e.x,dy=hero.y-e.y;
        if(e.r.searchX>0&&Math.abs(dx)<e.r.searchX*SCALE&&Math.abs(dy)<e.r.searchY*SCALE)e.target=true;
        if(Math.abs(dx)>e.r.escapeX*SCALE||Math.abs(dy)>e.r.escapeY*SCALE)e.target=false;
        if(e.stun<=0){
            if(e.action==='attack'){
                if(e.actionFrame===e.r.defHitSpd)e.hitWindow=2;
                e.vx=0;if(e.actionFrame>=e.r.defHitSpd+5){e.action='walk';e.actionFrame=0;}
            }else if(e.target){
                e.face=dx>=0?1:-1;
                const reach=(e.r.beHitRight+(e.name==='l3'?5:0)+15)*SCALE;
                if(Math.abs(dx)<=reach&&Math.abs(dy)<=30){e.action='attack';e.actionFrame=0;e.vx=0;}
                else {e.vx=e.face*e.r.thisSpeed*SCALE;e.action='walk';}
            }else{
                if(e.x<cameraX){e.face=1;e.vx=e.r.thisSpeed*SCALE;}
                else if(e.x>cameraX+screenW){e.face=-1;e.vx=-e.r.thisSpeed*SCALE;}
                else if(--e.freeTime<=0){e.freeTime=50;if(Math.random()<.5)e.vx=e.face*e.r.thisSpeed*SCALE;else {e.vx=0;e.face*=-1;}}
                e.action=e.vx?'walk':'idle';
            }
        }
        e.wall=false;moveBody(e,e.r.beHitRight,35);if(e.wall)e.face*=-1;
        if(e.y>worldH+40){e.x=clamp(cameraX+(Math.random()<.5?20:screenW-20),28,worldW-28);e.y=Math.min(worldH-40,cameraY+80);e.vy=0;}
        if(e.hitWindow>0){
            const reach=e.r.beHitRight*SCALE;
            if((hero.x-e.x)*e.face+7.5>=0&&(hero.x-e.x)*e.face-7.5<=reach&&Math.abs(hero.y-e.y)<30)heroHurt(e);
            e.hitWindow--;
        }
        e.actionFrame++;
    }
    enemies=enemies.filter(e=>e.hp>0||e.dying>0);
    for(const item of items){moveBody(item,8,8);item.life--;if(pickup&&Math.abs(item.x-hero.x)<=15&&Math.abs(item.y-hero.y)<=15)collect(item);}
    items=items.filter(i=>i.life>0);words=words.filter(w=>{w.life--;w.y-=.6;return w.life>0;}).slice(-12);
    if(hero.y>worldH+40)hero.hp=0;
    if(hero.hp<=0){endStage(false);return;}
    spawn();dirty=true;
}
function image(out,src,x,y,w,h,sx,sy,sw,sh,opacity){
    out.push({op:'drawImage',src,x:Math.round(x),y:Math.round(y),width:Math.round(w),height:Math.round(h),sourceX:sx,sourceY:sy,sourceWidth:sw,sourceHeight:sh,opacity:opacity===undefined?255:opacity});
}
function render(){
    if(!hero)return;
    const out=[];
    image(out,backgroundHQ.sky?'sky-hq':'sky',-cameraX/5,-cameraY/5,930,330,0,0,backgroundHQ.sky?930:96,backgroundHQ.sky?330:48);
    image(out,backgroundHQ.village?'village-hq':'village',-cameraX,17.5-cameraY,1620,385,0,0,backgroundHQ.village?1620:192,backgroundHQ.village?385:46);
    // Village bitmap contains the ground; draw elevated collision platforms
    // only where the original collision matrix has extra one-way surfaces.
    const x0=Math.max(0,Math.floor(cameraX/TILE)),x1=Math.min(current.bgSize[0]-1,Math.ceil((cameraX+screenW)/TILE));
    const y0=Math.max(0,Math.floor(cameraY/TILE)),y1=Math.min(current.bgSize[1]-1,Math.ceil((cameraY+fieldH)/TILE));
    for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){
        const c=current.bgCell[y][x];
        if(c===1&&y<15)image(out,'ground',x*TILE-cameraX,(y+1)*TILE-cameraY-5,TILE,5,0,0,16,16);
        if(c===3||c===4){out.push({op:'fillRect',x:Math.round(x*TILE-cameraX+8),y:Math.round(y*TILE-cameraY),width:4,height:20,color:0xa47e47});}
    }
    for(const item of items){const index=rules.itemNames.indexOf(item.name);if(index>=0)image(out,'items',item.x-cameraX-10,item.y-cameraY-20,20,20,index*12,0,12,12);}
    for(const e of enemies){
        const x=e.x-cameraX,y=e.y-cameraY;if(x< -60||x>screenW+60||y< -60||y>fieldH+60)continue;
        let f;
        if(e.hp<=0)f=10+Math.min(4,Math.floor(e.actionFrame/2));
        else if(e.stun>0)f=8+(e.actionFrame>=4?1:0);
        else if(e.action==='attack')f=e.actionFrame>=e.r.defHitSpd?16:15;
        else if(e.vx)f=4+Math.floor(e.actionFrame/2)%4;else f=Math.floor(e.actionFrame/4)%4;
        image(out,e.name,x-24,y-43,48,44,clamp(f,0,16)*18,e.face<0?22:0,18,22);
        if(e.target||e.protect)out.push({op:'fillRect',x:Math.round(x-16),y:Math.round(y-47),width:Math.max(1,Math.round(32*e.hp/e.r.thisAllHp)),height:2,color:0xe66c59});
    }
    let f=hero.onGround?(move?4+Math.floor(frame/4)%4:Math.floor(frame/4)%4):8;
    if(hero.actionLength>0){
        if(hero.action==='attack1')f=hero.actionFrame<3?9:10;
        else if(hero.action==='attack2')f=hero.actionFrame<3?11:12;
        else if(hero.action==='attack3')f=hero.actionFrame<3?13:14;
        else if(hero.action==='special')f=hero.actionFrame<6?11:12;
        else if(hero.action==='hurt')f=8;
    }
    if(hero.action==='roll')f=13+Math.floor(hero.actionFrame/2)%2;else if(hero.action==='charge')f=14;else if(hero.action==='slamRise'||hero.action==='slamFall')f=11;else if(hero.action==='slamLand')f=12;
    if(climb)f=15;
    if(hero.protect<=0||Math.floor(frame/2)%2===0)image(out,'hero',hero.x-cameraX-24,hero.y-cameraY-54,48,56,f*24,hero.face<0?28:0,24,28);
    if(hero.hitWindow>0){out.push({op:'drawArc',x:Math.round(hero.x-cameraX+hero.face*18),y:Math.round(hero.y-cameraY-22),radius:hero.action==='special'?60:30,startAngle:hero.face>0?280:100,endAngle:hero.face>0?80:260,lineWidth:3,color:0xffefbe,opacity:180});}
    for(const word of words)out.push({op:'drawText',x:Math.round(word.x-cameraX-20),y:Math.round(word.y-cameraY),width:100,height:24,text:word.text,color:word.color});
    if(skillFlash>0)out.push({op:'drawArc',x:Math.round(hero.x-cameraX),y:Math.round(hero.y-cameraY-20),radius:42,startAngle:0,endAngle:359,lineWidth:2,color:0xffdfa1});

    if(out.length>256)throw Error('Scene budget exceeded: '+out.length);
    lastSceneCommands=out.length;sceneSubmits++;Ouse.canvas.submit('world',out);dirty=false;
}
function drawHud(){
    if(hero&&hudView.draw({hp:Math.ceil(hero.hp),maxHp:maxHp(),mp:Math.floor(hero.mp),maxMp:maxMp(),xp,nextXp:50,level,kills,total:28},128))uiSubmits++;
}
function syncControls(){
 if(!uiActive||closed)return;const enabled=phase==='playing';
 for(const id of ['stick','attack','jump','skill0','skill1','skill2','skill3']){if(enabled)Ouse.ui.show(id);else Ouse.ui.hide(id);}
 for(let i=0;i<4;i++)Ouse.ui.setEnabled('skill'+i,!!(enabled&&hero&&level>=skillLevels[i]&&hero.mp>=skillCosts[i]));
 drawControls();
}
function drawControls(){
 if(!uiActive||closed)return;if(phase!=='playing'){controlCommands=[];Ouse.canvas.submit('hud',hudCommands);return;}
 const out=[],line=(x,y,ex,ey,color=0xf8e3b8,w=2)=>out.push({op:'drawLine',x:Math.round(x),y:Math.round(y),x2:Math.round(ex),y2:Math.round(ey),color,lineWidth:w});
 const ring=(x,y,r,color=0xe3d7b4)=>out.push({op:'drawArc',x:Math.round(x),y:Math.round(y),radius:r,startAngle:0,endAngle:359,lineWidth:2,color,opacity:170});
 ring(58,screenH-54,43);ring(58+move*20,screenH-54+climb*20,13);
 // Attack: sword silhouette. Jump: upward arrow. No solid button backgrounds.
 let x=screenW-45,y=screenH-45;ring(x,y,29);line(x-11,y+12,x+12,y-13);line(x-7,y-1,x+3,y+9);line(x+9,y-13,x+13,y-13);line(x+13,y-13,x+13,y-9);
 x=screenW-112;y=screenH-38;ring(x,y,26);line(x,y+12,x,y-12);line(x,y-12,x-10,y-2);line(x,y-12,x+10,y-2);
 for(let i=0;i<4;i++){x=screenW-27;y=28+i*45;const ready=level>=skillLevels[i],color=ready?0xffdfa0:0x9a9e94;ring(x,y,19,color);
  if(i===0){line(x-9,y+7,x+7,y-9,color,3);line(x-8,y-1,x,y+7,color);}
  if(i===1){out.push({op:'drawArc',x,y,radius:10,startAngle:35,endAngle:325,lineWidth:3,color});line(x+9,y-4,x+12,y+2,color);}
  if(i===2){line(x-12,y,x+10,y,color,3);line(x+10,y,x+3,y-7,color);line(x+10,y,x+3,y+7,color);}
  if(i===3){line(x,y-12,x,y+6,color,3);line(x,y+6,x-6,y,color);line(x,y+6,x+6,y,color);line(x-12,y+10,x+12,y+10,color);}
  out.push({op:'drawText',x:x-20,y:y+16,width:40,height:18,text:ready?String(skillCosts[i]):'Lv'+skillLevels[i],color});
 }
 controlCommands=out;Ouse.canvas.submit('hud',hudCommands.concat(out));
}

function stickPoint(p){
 if(phase!=='playing')return;pointer=true;const dx=p.x-46,dy=p.y-46,d=Math.hypot(dx,dy),scale=Math.max(30,d);
 move=Math.abs(dx)>9?Math.sign(dx):0;climb=Math.abs(dy)>12?Math.sign(dy):0;pickup=climb>0;
 drawControls();
}
function stickRelease(){pointer=null;move=climb=0;pickup=false;if(uiActive)drawControls();}
function attackDown(){if(phase!=='playing')return;attackHeld=true;beginAttack(false);dirty=true;}
function attackRelease(){attackHeld=false;}
function jumpPress(){if(phase==='playing')jumpPending=true;}
function layout(){
 const d=Ouse.display.get();if(d.orientation!=='landscape'){stopInput();Ouse.display.setOrientation('landscape');return;}
 screenW=Math.floor(d.contentWidthDp);screenH=Math.floor(d.contentHeightDp);fieldH=screenH;
 
 const frames=[{id:'world',x:0,y:0,width:screenW,height:fieldH},{id:'hud',x:0,y:0,width:screenW,height:screenH},{id:'pause',x:138,y:4,width:40,height:30},{id:'stick',x:12,y:screenH-100,width:92,height:92},{id:'attack',x:screenW-76,y:screenH-76,width:62,height:62},{id:'jump',x:screenW-140,y:screenH-66,width:56,height:56},{id:'modal',x:Math.floor((screenW-300)/2),y:Math.max(48,Math.floor((screenH-186)/2)),width:300,height:186}];
 for(let i=0;i<4;i++)frames.push({id:'skill'+i,x:screenW-49,y:8+i*45,width:44,height:42});
 Ouse.ui.layout(frames);hudView.invalidate();lastControls='';syncControls();dirty=true;
}

function tick(){
    if(closed)return;const now=Date.now(),ms=Math.max(0,now-last);last=now;
    pumpAudio(ms);
    if(phase==='playing'){
        accumulator+=Math.min(.125,ms/1000);let count=0;while(accumulator>=STEP&&count++<3){step();accumulator-=STEP;}
        hudTime+=ms;if(hudTime>=200){hudTime=0;drawHud();syncControls();}
        if(++statsFrames%100===0){
            const m=Ouse.media.get();
            console.log('BRAVE_FRAME '+JSON.stringify({frame,wave,kills,entities:enemies.length,sceneCommands:lastSceneCommands,sceneSubmits,hudSubmits:uiSubmits,effectsPlayed,media:audioStarted,positionMs:m?m.positionMs:0,audioError:m?m.errorCode:0}));
        }
    }
    if(dirty){render();drawHud();}
}
function startLoop(){if(!timer&&!closed){last=Date.now();timer=Ouse.timers.setInterval(tick,50);}}
function stopLoop(){if(timer)Ouse.timers.clear(timer);timer=0;stopInput();}
Ouse.ui.on('stick','pointerdown',stickPoint);Ouse.ui.on('stick','pointermove',stickPoint);Ouse.ui.on('stick','pointerup',stickRelease);Ouse.ui.on('stick','pointercancel',stickRelease);
Ouse.ui.on('attack','pointerdown',attackDown);Ouse.ui.on('attack','pointerup',attackRelease);Ouse.ui.on('attack','pointercancel',attackRelease);
Ouse.ui.on('jump','pointerdown',jumpPress);for(let i=0;i<4;i++)Ouse.ui.on('skill'+i,'click',()=>doSkill(i));
Ouse.ui.on('pause','click',()=>{if(phase==='playing')menu('冒险暂停');});
Ouse.ui.on('play','click',resume);Ouse.ui.on('restart','click',startStage);
Ouse.ui.on('stage','click',()=>{userTouched=true;soundOn=!soundOn;wantAudio('');if(!soundOn)Ouse.media.stop();Ouse.ui.setText('stage',soundOn?'声音：开':'声音：关');save();});
Ouse.display.onChange(()=>{stopInput();layout();});
globalThis.app={onPause(){uiActive=false;stopLoop();if(phase==='playing')phase='paused';Ouse.media.pause();},onResume(){uiActive=true;layout();if(!hero&&loaded){resetStage();menu();}else if(phase==='paused')menu('冒险暂停');startLoop();},onClose(){closed=true;uiActive=false;stopLoop();Ouse.media.stop();}};
Ouse.storage.get('first-stage-v2').then(data=>{
    if(closed||userTouched)return;
    if(data&&data.version===2){
        level=clamp(Number.isInteger(data.level)?data.level:1,1,40);xp=clamp(Number.isInteger(data.xp)?data.xp:0,0,49);
        for(const key of ['sword','shield','dress'])equipment[key]=clamp(data.equipment&&Number.isInteger(data.equipment[key])?data.equipment[key]:1,1,2);
        soundOn=data.soundOn!==false;
    }
}).catch(()=>{}).then(()=>{loaded=true;if(closed||userTouched)return;if(uiActive){layout();resetStage();menu();startLoop();}});
