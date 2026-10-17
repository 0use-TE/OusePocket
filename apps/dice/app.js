"use strict";
let values=[1], countIndex=0, rolling=false, rollTimer=0;

function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function text(id,value){Ouse.ui.setText(id,String(value));}
function count(){return countIndex+1;}
function face(v){return "face"+Math.min(6,Math.max(1,v|0));}
function rollOne(){return 1+Math.floor(Math.random()*6);}

function paintFaces(vals){
 const n=vals.length;
 if(n===1){
  Ouse.ui.setImage("dieSolo",face(vals[0]));
 }else if(n===2){
  Ouse.ui.setImage("dieL",face(vals[0]));
  Ouse.ui.setImage("dieR",face(vals[1]));
 }else{
  for(let i=0;i<n;i++)Ouse.ui.setImage("die"+i,face(vals[i]));
 }
}

function paintSum(vals){
 const n=vals.length;
 let total=0;for(let i=0;i<n;i++)total+=vals[i]|0;
 if(n===1)text("sum",String(vals[0]|0));
 else if(n===2)text("sum",vals[0]+" + "+vals[1]+" = "+total);
 else if(n===3)text("sum",vals.slice(0,3).join(" + ")+" = "+total);
 else text("sum",n+" 颗合计 "+total);
}

function paint(){
 const n=count();
 text("countBtn",n+"颗");
 visible("solo",n===1);
 visible("pair",n===2);
 visible("grid",n>=3);
 if(n>=3){
  visible("row1",n>3);
  for(let i=0;i<6;i++)visible("slot"+i,i<n);
 }
 paintFaces(values);
 paintSum(values);
}

function stopRoll(){
 if(rollTimer){Ouse.timers.clear(rollTimer);rollTimer=0;}
 rolling=false;
 Ouse.ui.setEnabled("roll",true);
 Ouse.ui.setEnabled("countBtn",true);
}

function roll(){
 if(rolling)return;
 const n=count();
 const final=[];
 for(let i=0;i<n;i++)final.push(rollOne());
 rolling=true;
 Ouse.ui.setEnabled("roll",false);
 Ouse.ui.setEnabled("countBtn",false);
 text("sum","...");
 let ticks=0;
 const totalTicks=20;
 rollTimer=Ouse.timers.setInterval(()=>{
  ticks++;
  if(ticks>=totalTicks){
   stopRoll();
   values=final;
   paint();
   Ouse.storage.set("dice",{version:3,count:countIndex,values:values});
   return;
  }
  const flash=[];
  for(let i=0;i<n;i++)flash.push(rollOne());
  paintFaces(flash);
 },100);
}

function setCount(index){
 if(rolling)return;
 countIndex=Math.min(5,Math.max(0,index|0));
 while(values.length<count())values.push(1);
 values=values.slice(0,count());
 Ouse.ui.hide("countSheet");
 paint();
 Ouse.storage.set("dice",{version:3,count:countIndex,values:values});
}

Ouse.ui.on("countBtn","click",()=>{if(!rolling)Ouse.ui.show("countSheet");});
for(let i=1;i<=6;i++){(function(n){Ouse.ui.on("pick"+n,"click",()=>setCount(n-1));})(i);}
Ouse.ui.on("roll","click",roll);
paint();
Ouse.storage.get("dice").then(value=>{
 if(!value||rolling)return;
 if(value.version===3||value.version===2||value.version===1){
  if(Number.isInteger(value.count)&&value.count>=0&&value.count<=5)countIndex=value.count;
  if(Array.isArray(value.values))values=value.values.filter(v=>Number.isInteger(v)&&v>=1&&v<=6).slice(0,6);
 }
 if(!values.length)values=[1];
 paint();
},()=>{});
