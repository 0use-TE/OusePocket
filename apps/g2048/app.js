"use strict";
const COLORS={
 0:["#cdc1b4","#776e65"],
 2:["#eee4da","#776e65"],
 4:["#ede0c8","#776e65"],
 8:["#f2b179","#f9f6f2"],
 16:["#f59563","#f9f6f2"],
 32:["#f67c5f","#f9f6f2"],
 64:["#f65e3b","#f9f6f2"],
 128:["#edcf72","#f9f6f2"],
 256:["#edcc61","#f9f6f2"],
 512:["#edc850","#f9f6f2"],
 1024:["#edc53f","#f9f6f2"],
 2048:["#edc22e","#f9f6f2"]
};
let grid=[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0], score=0, best=0, over=false, won=false;

function text(id,value){Ouse.ui.setText(id,String(value));}
function empty(){const out=[];for(let i=0;i<16;i++)if(!grid[i])out.push(i);return out;}
function spawn(){
 const slots=empty();
 if(!slots.length)return;
 grid[slots[Math.floor(Math.random()*slots.length)]]=Math.random()<0.9?2:4;
}
function colorOf(v){return COLORS[v]||["#3c3a32","#f9f6f2"];}
function paint(){
 for(let i=0;i<16;i++){
  const v=grid[i], pair=colorOf(v);
  text("c"+i,v?String(v):"");
  Ouse.ui.setStyle("c"+i,{backgroundColor:pair[0],textColor:pair[1],states:{pressed:{backgroundColor:pair[0],textColor:pair[1]}}});
 }
 text("score","分 "+score);
 text("best","最佳 "+best);
 if(over)text("status","没有空位了");
 else if(won)text("status","到 2048 了");
 else text("status","");
}
function slideLine(line){
 const nums=line.filter(v=>v);
 let gained=0, out=[];
 for(let i=0;i<nums.length;i++){
  if(i+1<nums.length&&nums[i]===nums[i+1]){out.push(nums[i]*2);gained+=nums[i]*2;if(nums[i]*2>=2048)won=true;i++;}
  else out.push(nums[i]);
 }
 while(out.length<4)out.push(0);
 return {line:out,gained:gained};
}
function rows(){
 return [
  [grid[0],grid[1],grid[2],grid[3]],
  [grid[4],grid[5],grid[6],grid[7]],
  [grid[8],grid[9],grid[10],grid[11]],
  [grid[12],grid[13],grid[14],grid[15]]
 ];
}
function writeRows(rs){
 for(let r=0;r<4;r++)for(let c=0;c<4;c++)grid[r*4+c]=rs[r][c];
}
function cols(){
 return [
  [grid[0],grid[4],grid[8],grid[12]],
  [grid[1],grid[5],grid[9],grid[13]],
  [grid[2],grid[6],grid[10],grid[14]],
  [grid[3],grid[7],grid[11],grid[15]]
 ];
}
function writeCols(cs){
 for(let c=0;c<4;c++)for(let r=0;r<4;r++)grid[r*4+c]=cs[c][r];
}
function same(a,b){for(let i=0;i<16;i++)if(a[i]!==b[i])return false;return true;}
function canMove(){
 if(empty().length)return true;
 for(let r=0;r<4;r++)for(let c=0;c<4;c++){
  const v=grid[r*4+c];
  if(c<3&&v===grid[r*4+c+1])return true;
  if(r<3&&v===grid[(r+1)*4+c])return true;
 }
 return false;
}
function move(dir){
 if(over)return;
 const before=grid.slice();
 let gained=0;
 if(dir==="left"||dir==="right"){
  const rs=rows();
  for(let i=0;i<4;i++){
   let line=rs[i];
   if(dir==="right")line=line.slice().reverse();
   const result=slideLine(line);
   gained+=result.gained;
   rs[i]=dir==="right"?result.line.slice().reverse():result.line;
  }
  writeRows(rs);
 }else{
  const cs=cols();
  for(let i=0;i<4;i++){
   let line=cs[i];
   if(dir==="down")line=line.slice().reverse();
   const result=slideLine(line);
   gained+=result.gained;
   cs[i]=dir==="down"?result.line.slice().reverse():result.line;
  }
  writeCols(cs);
 }
 if(same(before,grid))return;
 score+=gained;
 if(score>best)best=score;
 spawn();
 if(!canMove())over=true;
 paint();
 persist();
}
function restart(){
 grid=[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0];
 score=0;over=false;won=false;
 spawn();spawn();
 paint();
 persist();
}
function persist(){
 Ouse.storage.set("g2048",{version:1,grid:grid,score:score,best:best,over:over,won:won});
}

function onSwipe(dir){
 if(dir==="up"||dir==="down"||dir==="left"||dir==="right")move(dir);
}
Ouse.ui.on("board","swipe",onSwipe);
for(let i=0;i<16;i++)Ouse.ui.on("c"+i,"swipe",onSwipe);
for(let i=0;i<4;i++)Ouse.ui.on("r"+i,"swipe",onSwipe);
Ouse.ui.on("restart","click",restart);

Ouse.storage.get("g2048").then(value=>{
 if(value&&value.version===1&&Array.isArray(value.grid)&&value.grid.length===16){
  grid=value.grid.map(v=>Number(v)|0);
  score=Number(value.score)|0;
  best=Number(value.best)|0;
  over=!!value.over;
  won=!!value.won;
  if(!grid.some(v=>v)){spawn();spawn();}
  paint();
 }else restart();
},()=>restart());
