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
