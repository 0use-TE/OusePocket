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
