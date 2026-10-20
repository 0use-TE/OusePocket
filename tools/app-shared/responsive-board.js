// Embedded in each BPK; layout uses the active page and actual content viewport.
function installResponsiveBoard(config){
 function resize(){
  const page=Ouse.navigation.get().page,c=config[page]||config.main;if(!c)return;
  const d=Ouse.display.get(),w=Math.floor(d.contentWidthDp),h=Math.floor(d.contentHeightDp),land=w>h,a=[];
  function f(id,x,y,width,height){a.push({id,x,y,width:Math.max(1,Math.floor(width)),height:Math.max(1,Math.floor(height))});}
  const before=c.before,after=c.after,top=before.reduce((sum,n)=>sum+n.height+6,8),bottom=after.reduce((sum,n)=>sum+n.height+6,8);
  // Compute each axis separately for rectangular boards such as Klotski.
  const size=Math.max(1,Math.floor(Math.min(((land?w-244:w-16)-2*c.padding-(c.cols-1)*c.gap)/c.cols,((land?h-16:h-top-bottom)-2*c.padding-(c.rows-1)*c.gap)/c.rows)));
  const bw=2*c.padding+c.cols*size+(c.cols-1)*c.gap,bh=2*c.padding+c.rows*size+(c.rows-1)*c.gap,bx=land?8:Math.floor((w-bw)/2),by=land?8:top;
  f(c.board,bx,by,bw,bh);
  let y=8;for(const n of before){f(n.id,land?bw+20:8,y,land?w-bw-28:w-16,n.height);y+=n.height+6;}
  y=land?y+6:by+bh+6;for(const n of after){f(n.id,land?bw+20:8,y,land?w-bw-28:w-16,n.height);y+=n.height+6;}
  for(let r=0;r<c.rows;r++){
   f(c.rowIds[r],c.padding,c.padding+r*(size+c.gap),bw-2*c.padding,size);
   for(let col=0;col<c.cols;col++)f(c.cells[r][col],col*(size+c.gap),0,size,size);
  }
  for(let i=0;i<a.length;i+=64)Ouse.ui.layout(a.slice(i,i+64));
 }
 Ouse.display.onChange(resize);
 const previous=globalThis.app||{},resume=previous.onResume;
 previous.onResume=function(){if(resume)resume();resize();};globalThis.app=previous;
 resize();return resize;
}
