"use strict";
let secret=0, tries=0, won=false;

function text(id,value){Ouse.ui.setText(id,String(value));}
function roll(){return 1+Math.floor(Math.random()*100);}

function paint(){
 text("tries","已猜 "+tries+" 次");
 if(won)text("hint","对了!");
 Ouse.ui.setEnabled("guess",!won);
 Ouse.ui.setEnabled("input",!won);
}

function restart(){
 secret=roll();
 tries=0;
 won=false;
 Ouse.ui.setText("input","");
 text("hint","猜一个数");
 paint();
}

function submit(){
 if(won)return;
 const raw=Ouse.ui.getText("input").trim();
 const n=parseInt(raw,10);
 if(!Number.isInteger(n)||n<1||n>100){text("hint","输入 1-100");return;}
 tries++;
 if(n===secret){won=true;text("hint","对了!");}
 else if(n<secret)text("hint","太小了");
 else text("hint","太大了");
 paint();
}

Ouse.ui.on("guess","click",submit);
Ouse.ui.on("again","click",restart);
Ouse.ui.on("input","submit",submit);
restart();

// BEGIN responsive simple game
// Embedded source: preserve game state and lifecycle while rearranging controls.
function installSimpleGameLayout(kind){
 function resize(){
  const d=Ouse.display.get();if(d.keyboardVisible)return;
  const w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),land=w>h,a=[];
  function f(id,x,y,width,height){a.push({id,x,y,width,height});}
  if(kind==='dice'){
   f('header',land?264:8,8,land?w-272:w-16,40);f('stage',8,land?8:54,land?240:w-16,land?h-16:h-172);
   f('sum',land?264:8,land?64:h-108,land?w-272:w-16,36);f('roll',land?264:8,h-64,land?w-272:w-16,56);
  }else if(kind==='rps'){
   f('heading',land?268:8,8,land?w-276:w-16,40);f('score',land?268:8,land?56:54,land?w-276:w-16,28);
   f('stage',8,land?8:88,land?244:w-16,land?h-16:h-160);f('actions',land?268:8,land?96:h-64,land?w-276:w-16,56);
  }else if(kind==='reaction'){
   f('heading',8,8,land?184:w-16,40);f('best',8,54,land?184:w-16,28);f('result',8,h-48,land?184:w-16,40);
   f('pad',land?208:8,land?8:88,land?w-216:w-16,land?h-16:h-144);
  }else{
   const sw=w-16,column=land?Math.floor(sw/2)-16:sw-16,right=land?Math.floor(sw/2)+8:8;
   f('heading',8,8,w-16,40);f('stage',8,56,sw,h-64);
   f('range',8,8,column,36);f('hint',8,52,column,48);f('tries',8,108,column,32);f('spacer',0,0,1,1);
   f('input',right,land?24:164,column,48);f('guess',right,land?88:220,column,56);
  }
  Ouse.ui.layout(a);
 }
 Ouse.display.onChange(resize);const app=globalThis.app||{},resume=app.onResume;
 app.onResume=function(){if(resume)resume();resize();};globalThis.app=app;resize();
}

installSimpleGameLayout('guess');
// END responsive simple game
