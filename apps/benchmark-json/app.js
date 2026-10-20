"use strict";
// Shared by both apps AND the native host benchmark. No random or elapsed-time
// dependency: the same frame/count/dimensions always produces the same scene.
function benchmarkScene(count, frame, width, height) {
    const actors = [];
    for (let i = 0; i < count; i++) {
        const periodX = 2 * (width - 16), periodY = 2 * (height - 16);
        let x = (i * 47 + frame * (1 + i % 3)) % periodX;
        let y = (i * 31 + frame * (1 + i % 2)) % periodY;
        if (x > width - 16) x = periodX - x;
        if (y > height - 16) y = periodY - y;
        actors.push({ id: 'e' + i, x, y, frame: (i + Math.floor(frame / 4)) % 4 });
    }
    return actors;
}
function benchmarkSubmit(mode, actors) {
    if (mode === 'canvas') {
        Ouse.canvas.submit('world', actors.map(a => ({
            op: 'drawImage', src: 'actors', x: a.x, y: a.y, width: 16, height: 16,
            sourceX: a.frame % 2 * 16, sourceY: Math.floor(a.frame / 2) * 16,
            sourceWidth: 16, sourceHeight: 16
        })));
    } else {
        for (let i = 0; i < actors.length; i += 32)
            Ouse.sprites.update(actors.slice(i, i + 32));
    }
}

const MODE="json";

let timer=0, frame=0, stage=0, width=320, height=222, total=0, maximum=0;
const counts=[8,32,64,96];
function configure() {
    const d=Ouse.display.get();
    if(d.orientation!=='landscape'){Ouse.display.setOrientation('landscape');return;}
    width=Math.floor(d.contentWidthDp);height=Math.floor(d.contentHeightDp)-34;
    Ouse.ui.layout([{id:'world',x:0,y:34,width,height},{id:'status',x:4,y:0,width:width-8,height:28}]);
    frame=0;total=maximum=0;
    if(MODE==='json')for(let i=0;i<96;i++){
        if(i<counts[stage])Ouse.ui.show('e'+i);else Ouse.ui.hide('e'+i);
    }
    console.log('BENCH_STAGE '+JSON.stringify({mode:MODE,count:counts[stage],width,height,timeMs:Date.now()}));
}
function tick() {
    const started=Date.now();
    benchmarkSubmit(MODE,benchmarkScene(counts[stage],frame,width,height));
    const ms=Date.now()-started;total+=ms;maximum=Math.max(maximum,ms);frame++;
    if(frame%20===0)Ouse.ui.setText('status',MODE+' N='+counts[stage]+' submit avg '+(total/frame).toFixed(1)+' max '+maximum+'ms');
    if(frame===200){
        console.log('BENCH_SUBMIT '+JSON.stringify({mode:MODE,count:counts[stage],frames:frame,meanMs:total/frame,maxMs:maximum}));
        if(stage===counts.length-1){stop();return;}
        stage=(stage+1)%counts.length;configure();
    }
}
function start(){if(!timer)timer=Ouse.timers.setInterval(tick,50);}
function stop(){if(timer)Ouse.timers.clear(timer);timer=0;}
globalThis.app={onCreate(){configure();start();},onResume(){configure();start();},onPause:stop,onClose:stop};
Ouse.display.onChange(configure);
configure();start();
